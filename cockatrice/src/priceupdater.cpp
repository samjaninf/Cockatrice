/**
 * @author Marcio Ribeiro <mmr@b1n.org>, Max-Wilhelm Bruker <brukie@gmx.net>
 * @version 1.1
 */
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMessageBox>

#include "qt-json/json.h"
#include "priceupdater.h"

#if QT_VERSION < 0x050000
    // for Qt::escape() 
    #include <QtGui/qtextdocument.h>
#endif

/**
 * Constructor.
 *
 * @param _deck deck.
 */
AbstractPriceUpdater::AbstractPriceUpdater(const DeckList *_deck)
{
    nam = new QNetworkAccessManager(this);
    deck = _deck;
}

// blacklotusproject.com

/**
 * Constructor.
 *
 * @param _deck deck.
 */
BLPPriceUpdater::BLPPriceUpdater(const DeckList *_deck)
: AbstractPriceUpdater(_deck)
{
}

/**
 * Update the prices of the cards in deckList.
 */
void BLPPriceUpdater::updatePrices()
{
    QString q = "http://blacklotusproject.com/json/?cards=";
    QStringList cards = deck->getCardList();
    for (int i = 0; i < cards.size(); ++i) {
        q += cards[i].toLower() + "|";
    }
    QUrl url(q.replace(' ', '+'));

    QNetworkReply *reply = nam->get(QNetworkRequest(url));
    connect(reply, SIGNAL(finished()), this, SLOT(downloadFinished()));
}

/**
 * Called when the download of the json file with the prices is finished.
 */
void BLPPriceUpdater::downloadFinished()
{
    QNetworkReply *reply = static_cast<QNetworkReply *>(sender());
    bool ok;
    QVariantMap resultMap = QtJson::Json::parse(QString(reply->readAll()), ok).toMap();
    if (!ok) {
        reply->deleteLater();
        deleteLater();
        return;
    }
    
    QMap<QString, float> cardsPrice;
    
    QListIterator<QVariant> it(resultMap.value("cards").toList());
    while (it.hasNext()) {
        QVariantMap map = it.next().toMap();
        QString name = map.value("name").toString().toLower();
        float price = map.value("price").toString().toFloat();
        QString set = map.value("set_code").toString();

        /**
        * Make sure Masters Edition (MED) isn't the set, as it doesn't
        * physically exist. Also check the price to see that the cheapest set
        * ends up as the final price.
        */
        if (set != "MED" && (!cardsPrice.contains(name) || cardsPrice.value(name) > price))
            cardsPrice.insert(name, price);
    }
    
    InnerDecklistNode *listRoot = deck->getRoot();
    for (int i = 0; i < listRoot->size(); i++) {
        InnerDecklistNode *currentZone = dynamic_cast<InnerDecklistNode *>(listRoot->at(i));
        for (int j = 0; j < currentZone->size(); j++) {
            DecklistCardNode *currentCard = dynamic_cast<DecklistCardNode *>(currentZone->at(j));
            if (!currentCard)
                continue;
            currentCard->setPrice(cardsPrice[currentCard->getName().toLower()]);
        }
    }
    
    reply->deleteLater();
    deleteLater();
    emit finishedUpdate();
}

// deckbrew.com

/**
 * Constructor.
 *
 * @param _deck deck.
 */
DBPriceUpdater::DBPriceUpdater(const DeckList *_deck)
: AbstractPriceUpdater(_deck)
{
}

/**
 * Update the prices of the cards in deckList.
 */
void DBPriceUpdater::updatePrices()
{
    QString q = "https://api.deckbrew.com/mtg/cards";
    QStringList cards = deck->getCardList();
    for (int i = 0; i < cards.size(); ++i) {
        q += (i ? "&" : "?")  + QString("name=") + cards[i].toLower();
    }
    QUrl url(q.replace(' ', '+'));

    QNetworkReply *reply = nam->get(QNetworkRequest(url));
    connect(reply, SIGNAL(finished()), this, SLOT(downloadFinished()));
}

/**
 * Called when the download of the json file with the prices is finished.
 */
void DBPriceUpdater::downloadFinished()
{
    QNetworkReply *reply = static_cast<QNetworkReply *>(sender());
    bool ok;
    QString tmp = QString(reply->readAll());

    // Errors are incapsulated in an object, check for them first
    QVariantMap resultMap = QtJson::Json::parse(tmp, ok).toMap();
    if (!ok) {
        QMessageBox::critical(this, tr("Error"), tr("A problem has occured while fetching card prices."));
        reply->deleteLater();
        deleteLater();
        return;
    }

    if(resultMap.contains("errors"))
    {
        QMessageBox::critical(this, tr("Error"), tr("A problem has occured while fetching card prices:") + 
            "<br/>" +
#if QT_VERSION < 0x050000
            Qt::escape(resultMap["errors"].toList().first().toString())
#else
            resultMap["errors"].toList().first().toString().toHtmlEscaped()
#endif
        );
        reply->deleteLater();
        deleteLater();
        return;
    }

    // Good results are a list
    QVariantList resultList = QtJson::Json::parse(tmp, ok).toList();
    if (!ok) {
        QMessageBox::critical(this, tr("Error"), tr("A problem has occured while fetching card prices."));
        reply->deleteLater();
        deleteLater();
        return;
    }

    QMap<QString, float> cardsPrice;

    QListIterator<QVariant> it(resultList);
    while (it.hasNext()) {
        QVariantMap map = it.next().toMap();
        QString name = map.value("name").toString().toLower();

        QList<QVariant> editions = map.value("editions").toList();
        foreach (QVariant ed, editions)
        {
            QVariantMap edition = ed.toMap();
            QString set = edition.value("set_id").toString();
            // Prices are in USD cents
            float price = edition.value("price").toMap().value("median").toString().toFloat() / 100;
            //qDebug() << "card " << name << " set " << set << " price " << price << endl;

            /**
            * Make sure Masters Edition (MED) isn't the set, as it doesn't
            * physically exist. Also check the price to see that the cheapest set
            * ends up as the final price.
            */
            if (set != "MED" && (!cardsPrice.contains(name) || cardsPrice.value(name) > price))
                cardsPrice.insert(name, price);
        }
    }
    
    InnerDecklistNode *listRoot = deck->getRoot();
    for (int i = 0; i < listRoot->size(); i++) {
        InnerDecklistNode *currentZone = dynamic_cast<InnerDecklistNode *>(listRoot->at(i));
        for (int j = 0; j < currentZone->size(); j++) {
            DecklistCardNode *currentCard = dynamic_cast<DecklistCardNode *>(currentZone->at(j));
            if (!currentCard)
                continue;
            currentCard->setPrice(cardsPrice[currentCard->getName().toLower()]);
        }
    }
    
    reply->deleteLater();
    deleteLater();
    emit finishedUpdate();
}

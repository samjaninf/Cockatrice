#ifndef PRICEUPDATER_H
#define PRICEUPDATER_H

#include <QNetworkReply>
#include "decklist.h"

class QNetworkAccessManager;

/**
 * Price Updater.
 *
 * @author Marcio Ribeiro <mmr@b1n.org>
 */
class AbstractPriceUpdater : public QWidget
{
    Q_OBJECT
protected:
    const DeckList *deck;
    QNetworkAccessManager *nam;
signals:
    void finishedUpdate();
protected slots:
    virtual void downloadFinished() = 0;
public:
    AbstractPriceUpdater(const DeckList *deck);
    virtual void updatePrices() = 0;
};

class BLPPriceUpdater : public AbstractPriceUpdater
{
    Q_OBJECT
protected:
    virtual void downloadFinished();
public:
    BLPPriceUpdater(const DeckList *deck);
    virtual void updatePrices();
};

class DBPriceUpdater : public AbstractPriceUpdater
{
    Q_OBJECT
protected:
    virtual void downloadFinished();
public:
    DBPriceUpdater(const DeckList *deck);
    virtual void updatePrices();
};
#endif

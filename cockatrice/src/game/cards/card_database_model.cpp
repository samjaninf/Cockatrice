#include "card_database_model.h"

#include "../filters/filter_tree.h"

#include <QMap>

#define CARDDBMODEL_COLUMNS 6

CardDatabaseModel::CardDatabaseModel(CardDatabase *_db, bool _showOnlyCardsFromEnabledSets, QObject *parent)
    : QAbstractListModel(parent), db(_db), showOnlyCardsFromEnabledSets(_showOnlyCardsFromEnabledSets)
{
    connect(db, &CardDatabase::cardAdded, this, &CardDatabaseModel::cardAdded);
    connect(db, &CardDatabase::cardRemoved, this, &CardDatabaseModel::cardRemoved);
    connect(db, &CardDatabase::cardDatabaseEnabledSetsChanged, this,
            &CardDatabaseModel::cardDatabaseEnabledSetsChanged);

    cardDatabaseEnabledSetsChanged();
}

CardDatabaseModel::~CardDatabaseModel() = default;

QMap<wchar_t, wchar_t> CardDatabaseDisplayModel::characterTranslation = {{L'“', L'\"'},
                                                                         {L'”', L'\"'},
                                                                         {L'‘', L'\''},
                                                                         {L'’', L'\''}};

int CardDatabaseModel::rowCount(const QModelIndex & /*parent*/) const
{
    return cardList.size();
}

int CardDatabaseModel::columnCount(const QModelIndex & /*parent*/) const
{
    return CARDDBMODEL_COLUMNS;
}

QVariant CardDatabaseModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= cardList.size() || index.column() >= CARDDBMODEL_COLUMNS ||
        (role != Qt::DisplayRole && role != SortRole))
        return QVariant();

    CardInfoPtr card = cardList.at(index.row());
    switch (index.column()) {
        case NameColumn:
            return card->getName();
        case SetListColumn:
            return card->getSetsNames();
        case ManaCostColumn:
            return role == SortRole ? QString("%1%2").arg(card->getCmc(), 4, QChar('0')).arg(card->getManaCost())
                                    : card->getManaCost();
        case CardTypeColumn:
            return card->getCardType();
        case PTColumn:
            return card->getPowTough();
        case ColorColumn:
            return card->getColors();
        default:
            return QVariant();
    }
}

QVariant CardDatabaseModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();
    if (orientation != Qt::Horizontal)
        return QVariant();
    switch (section) {
        case NameColumn:
            return QString(tr("Name"));
        case SetListColumn:
            return QString(tr("Sets"));
        case ManaCostColumn:
            return QString(tr("Mana cost"));
        case CardTypeColumn:
            return QString(tr("Card type"));
        case PTColumn:
            return QString(tr("P/T"));
        case ColorColumn:
            return QString(tr("Color(s)"));
        default:
            return QVariant();
    }
}

void CardDatabaseModel::cardInfoChanged(CardInfoPtr card)
{
    const int row = cardList.indexOf(card);
    if (row == -1)
        return;

    emit dataChanged(index(row, 0), index(row, CARDDBMODEL_COLUMNS - 1));
}

bool CardDatabaseModel::checkCardHasAtLeastOneEnabledSet(CardInfoPtr card)
{
    if (!showOnlyCardsFromEnabledSets)
        return true;

    for (const auto &printings : card->getSets()) {
        for (const auto &printing : printings) {
            if (printing.getSet()->getEnabled())
                return true;
        }
    }

    return false;
}

void CardDatabaseModel::cardDatabaseEnabledSetsChanged()
{
    // remove all the cards no more present in at least one enabled set
    for (const CardInfoPtr &card : cardList) {
        if (!checkCardHasAtLeastOneEnabledSet(card)) {
            cardRemoved(card);
        }
    }

    // re-check all the card currently not shown, maybe their part of a newly-enabled set
    for (const CardInfoPtr &card : db->getCardList()) {
        if (!cardListSet.contains(card)) {
            cardAdded(card);
        }
    }
}

void CardDatabaseModel::cardAdded(CardInfoPtr card)
{
    if (checkCardHasAtLeastOneEnabledSet(card)) {
        // add the card if it's present in at least one enabled set
        beginInsertRows(QModelIndex(), cardList.size(), cardList.size());
        cardList.append(card);
        cardListSet.insert(card);
        connect(card.data(), &CardInfo::cardInfoChanged, this, &CardDatabaseModel::cardInfoChanged);
        endInsertRows();
    }
}

void CardDatabaseModel::cardRemoved(CardInfoPtr card)
{
    const int row = cardList.indexOf(card);
    if (row == -1) {
        return;
    }

    beginRemoveRows(QModelIndex(), row, row);
    disconnect(card.data(), nullptr, this, nullptr);
    cardListSet.remove(card);
    card.clear();
    cardList.removeAt(row);
    endRemoveRows();
}

CardDatabaseDisplayModel::CardDatabaseDisplayModel(QObject *parent)
    : QSortFilterProxyModel(parent), isToken(ShowAll), filterString(nullptr)
{
    filterTree = nullptr;
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSortCaseSensitivity(Qt::CaseInsensitive);

    dirtyTimer.setSingleShot(true);
    connect(&dirtyTimer, &QTimer::timeout, this, &CardDatabaseDisplayModel::invalidate);

    loadedRowCount = 0;
}

bool CardDatabaseDisplayModel::canFetchMore(const QModelIndex &index) const
{
    return loadedRowCount < sourceModel()->rowCount(index);
}

void CardDatabaseDisplayModel::fetchMore(const QModelIndex &index)
{
    int remainder = sourceModel()->rowCount(index) - loadedRowCount;
    int itemsToFetch = qMin(100, remainder);

    if (itemsToFetch == 0) {
        return;
    }

    const auto startIndex = qMin(rowCount(QModelIndex()), loadedRowCount);
    beginInsertRows(QModelIndex(), startIndex, startIndex + itemsToFetch - 1);

    loadedRowCount += itemsToFetch;
    endInsertRows();
}

int CardDatabaseDisplayModel::rowCount(const QModelIndex &parent) const
{
    return QSortFilterProxyModel::rowCount(parent);
}

bool CardDatabaseDisplayModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{

    QString leftString = sourceModel()->data(left, CardDatabaseModel::SortRole).toString();
    QString rightString = sourceModel()->data(right, CardDatabaseModel::SortRole).toString();

    if (!cardName.isEmpty() && left.column() == CardDatabaseModel::NameColumn) {
        bool isLeftType = leftString.startsWith(cardName, Qt::CaseInsensitive);
        bool isRightType = rightString.startsWith(cardName, Qt::CaseInsensitive);

        // test for an exact match: isLeftType && leftString.size() == cardName.size()
        // or an exclusive start match: isLeftType && !isRightType
        if (isLeftType && (!isRightType || leftString.size() == cardName.size()))
            return true;

        // same checks for the right string
        if (isRightType && (!isLeftType || rightString.size() == cardName.size()))
            return false;
    } else if (right.column() == CardDatabaseModel::PTColumn && left.column() == CardDatabaseModel::PTColumn) {
        QStringList leftList = leftString.split("/");
        QStringList rightList = rightString.split("/");

        if (leftList.size() == 2 && rightList.size() == 2) {

            // cool, have both P/T in list now
            int lessThanNum = lessThanNumerically(leftList.at(0), rightList.at(0));
            if (lessThanNum != 0) {
                return lessThanNum < 0;
            } else {
                // power equal, check toughness
                return lessThanNumerically(leftList.at(1), rightList.at(1)) < 0;
            }
        }
    }
    return QString::localeAwareCompare(leftString, rightString) < 0;
}

int CardDatabaseDisplayModel::lessThanNumerically(const QString &left, const QString &right)
{
    if (left == right) {
        return 0;
    }

    bool okLeft, okRight;
    float leftNum = left.toFloat(&okLeft);
    float rightNum = right.toFloat(&okRight);

    if (okLeft && okRight) {
        if (leftNum < rightNum) {
            return -1;
        } else if (leftNum > rightNum) {
            return 1;
        } else {
            return 0;
        }
    }
    // try and parsing again, for weird ones like "1+*"
    QString leftAfterNum = "";
    QString rightAfterNum = "";
    if (!okLeft) {
        int leftNumIndex = 0;
        for (; leftNumIndex < left.length(); leftNumIndex++) {
            if (!left.at(leftNumIndex).isDigit()) {
                break;
            }
        }
        if (leftNumIndex != 0) {
            leftNum = left.left(leftNumIndex).toFloat(&okLeft);
            leftAfterNum = left.right(leftNumIndex);
        }
    }
    if (!okRight) {
        int rightNumIndex = 0;
        for (; rightNumIndex < right.length(); rightNumIndex++) {
            if (!right.at(rightNumIndex).isDigit()) {
                break;
            }
        }
        if (rightNumIndex != 0) {
            rightNum = right.left(rightNumIndex).toFloat(&okRight);
            rightAfterNum = right.right(rightNumIndex);
        }
    }
    if (okLeft && okRight) {

        if (leftNum != rightNum) {
            // both parsed as numbers, but different number
            if (leftNum < rightNum) {
                return -1;
            } else {
                return 1;
            }
        } else {
            // both parsed, same number, but at least one has something else
            // so compare the part after the number - prefer nothing
            return QString::localeAwareCompare(leftAfterNum, rightAfterNum);
        }
    } else if (okLeft) {
        return -1;
    } else if (okRight) {
        return 1;
    }
    // couldn't parse it, just return String comparison
    return QString::localeAwareCompare(left, right);
}
bool CardDatabaseDisplayModel::filterAcceptsRow(int sourceRow, const QModelIndex & /*sourceParent*/) const
{
    CardInfoPtr info = static_cast<CardDatabaseModel *>(sourceModel())->getCard(sourceRow);

    if (((isToken == ShowTrue) && !info->getIsToken()) || ((isToken == ShowFalse) && info->getIsToken()))
        return false;

    if (filterString != nullptr) {
        if (filterTree != nullptr && !filterTree->acceptsCard(info)) {
            return false;
        }
        return filterString->check(info);
    }

    return rowMatchesCardName(info);
}

bool CardDatabaseDisplayModel::rowMatchesCardName(CardInfoPtr info) const
{
    if (!cardName.isEmpty() && !info->getName().contains(cardName, Qt::CaseInsensitive))
        return false;

    if (!cardNameSet.isEmpty() && !cardNameSet.contains(info->getName()))
        return false;

    if (filterTree != nullptr)
        return filterTree->acceptsCard(info);

    return true;
}

void CardDatabaseDisplayModel::clearFilterAll()
{
    cardName.clear();
    cardText.clear();
    cardTypes.clear();
    cardColors.clear();
    if (filterTree != nullptr)
        filterTree->clear();
    invalidateFilter();
}

void CardDatabaseDisplayModel::setFilterTree(FilterTree *_filterTree)
{
    if (this->filterTree != nullptr)
        disconnect(this->filterTree, nullptr, this, nullptr);

    this->filterTree = _filterTree;
    connect(this->filterTree, &FilterTree::changed, this, &CardDatabaseDisplayModel::filterTreeChanged);
    invalidate();
}

void CardDatabaseDisplayModel::filterTreeChanged()
{
    invalidate();
}

const QString CardDatabaseDisplayModel::sanitizeCardName(const QString &dirtyName, const QMap<wchar_t, wchar_t> &table)
{
    std::wstring toReturn = dirtyName.toStdWString();
    for (wchar_t &ch : toReturn) {
        if (table.contains(ch)) {
            ch = table.value(ch);
        }
    }
    return QString::fromStdWString(toReturn);
}

TokenDisplayModel::TokenDisplayModel(QObject *parent) : CardDatabaseDisplayModel(parent)
{
}

bool TokenDisplayModel::filterAcceptsRow(int sourceRow, const QModelIndex & /*sourceParent*/) const
{
    CardInfoPtr info = static_cast<CardDatabaseModel *>(sourceModel())->getCard(sourceRow);
    return info->getIsToken() && rowMatchesCardName(info);
}

int TokenDisplayModel::rowCount(const QModelIndex &parent) const
{
    // always load all tokens at start
    return QSortFilterProxyModel::rowCount(parent);
}

TokenEditModel::TokenEditModel(QObject *parent) : CardDatabaseDisplayModel(parent)
{
}

bool TokenEditModel::filterAcceptsRow(int sourceRow, const QModelIndex & /*sourceParent*/) const
{
    CardInfoPtr info = static_cast<CardDatabaseModel *>(sourceModel())->getCard(sourceRow);
    return info->getIsToken() && info->getSets().contains(CardSet::TOKENS_SETNAME) && rowMatchesCardName(info);
}

int TokenEditModel::rowCount(const QModelIndex &parent) const
{
    // always load all tokens at start
    return QSortFilterProxyModel::rowCount(parent);
}

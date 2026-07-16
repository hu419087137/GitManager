#include "CommitGraphWidget.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kHistoryPageSize = 200;
constexpr int kQueryDebounceMs = 275;
const QString kSettingsGroup = QStringLiteral("CommitGraphWidget");
}

// ============================================================
// CommitGraphWidget
// ============================================================

CommitGraphWidget::CommitGraphWidget(QWidget* parent)
    : QWidget(parent)
{
    _model    = new CommitGraphModel(this);
    _delegate = new CommitGraphDelegate(_model, this);
    _model->setObjectName(QStringLiteral("commitGraphModel"));

    _searchEdit = new QLineEdit(this);
    _searchEdit->setObjectName(QStringLiteral("commitSearchEdit"));
    _searchEdit->setClearButtonEnabled(true);
    _searchEdit->setPlaceholderText(QStringLiteral("Search commits"));

    _branchCombo = new QComboBox(this);
    _branchCombo->setObjectName(QStringLiteral("commitBranchCombo"));
    _branchCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _branchCombo->setMinimumContentsLength(16);
    _branchCombo->addItem(QStringLiteral("Current branch (HEAD)"), QString());
    _branchCombo->addItem(QStringLiteral("All branches"), QStringLiteral("*"));

    _authorEdit = new QLineEdit(this);
    _authorEdit->setObjectName(QStringLiteral("commitAuthorEdit"));
    _authorEdit->setClearButtonEnabled(true);
    _authorEdit->setPlaceholderText(QStringLiteral("Author"));

    _pathEdit = new QLineEdit(this);
    _pathEdit->setObjectName(QStringLiteral("commitPathEdit"));
    _pathEdit->setClearButtonEnabled(true);
    _pathEdit->setPlaceholderText(QStringLiteral("Path"));

    _fromDateCheck = new QCheckBox(QStringLiteral("From"), this);
    _fromDateCheck->setObjectName(QStringLiteral("commitFromDateCheck"));
    _fromDateEdit = new QDateEdit(QDate::currentDate().addYears(-1), this);
    _fromDateEdit->setObjectName(QStringLiteral("commitFromDateEdit"));
    _fromDateEdit->setCalendarPopup(true);
    _fromDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    _fromDateEdit->setEnabled(false);

    _toDateCheck = new QCheckBox(QStringLiteral("To"), this);
    _toDateCheck->setObjectName(QStringLiteral("commitToDateCheck"));
    _toDateEdit = new QDateEdit(QDate::currentDate(), this);
    _toDateEdit->setObjectName(QStringLiteral("commitToDateEdit"));
    _toDateEdit->setCalendarPopup(true);
    _toDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    _toDateEdit->setEnabled(false);

    _orderCombo = new QComboBox(this);
    _orderCombo->setObjectName(QStringLiteral("commitOrderCombo"));
    _orderCombo->addItem(QStringLiteral("Newest first"), false);
    _orderCombo->addItem(QStringLiteral("Oldest first"), true);

    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    _orderCombo->setCurrentIndex(settings.value(QStringLiteral("oldestFirst"), false).toBool() ? 1 : 0);

    auto* viewButton = new QToolButton(this);
    viewButton->setAutoRaise(true);
    viewButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    viewButton->setToolTip(QStringLiteral("Commit history display options"));
    viewButton->setPopupMode(QToolButton::InstantPopup);
    auto* viewMenu = new QMenu(viewButton);
    _showGraphAction = viewMenu->addAction(QStringLiteral("Show commit graph"));
    _showGraphAction->setCheckable(true);
    _showGraphAction->setChecked(settings.value(QStringLiteral("showGraph"), true).toBool());
    _showRefsAction = viewMenu->addAction(QStringLiteral("Show reference labels"));
    _showRefsAction->setCheckable(true);
    _showRefsAction->setChecked(settings.value(QStringLiteral("showReferences"), true).toBool());
    viewButton->setMenu(viewMenu);
    _model->setShowReferences(_showRefsAction->isChecked());

    _view = new QTreeView(this);
    _view->setObjectName(QStringLiteral("commitHistoryView"));
    _view->setModel(_model);
    _view->setItemDelegate(_delegate);
    _view->setRootIsDecorated(false);
    _view->setAlternatingRowColors(true);
    _view->setSelectionBehavior(QAbstractItemView::SelectRows);
    _view->setSelectionMode(QAbstractItemView::SingleSelection);
    _view->setUniformRowHeights(true);
    _view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _view->setAllColumnsShowFocus(true);

    // Header size/order is restored once here and never changed by page updates.
    _view->header()->setStretchLastSection(false);
    _view->header()->setSectionsMovable(true);
    _view->header()->setSectionResizeMode(QHeaderView::Interactive);
    const QByteArray headerState = settings.value(QStringLiteral("headerState")).toByteArray();
    if (headerState.isEmpty() || !_view->header()->restoreState(headerState)) {
        _view->setColumnWidth(CommitGraphModel::ColGraph, 100);
        _view->setColumnWidth(CommitGraphModel::ColHash, 80);
        _view->setColumnWidth(CommitGraphModel::ColMessage, 420);
        _view->setColumnWidth(CommitGraphModel::ColAuthor, 130);
        _view->setColumnWidth(CommitGraphModel::ColDate, 145);
    }
    settings.endGroup();

    _loadedLabel = new QLabel(QStringLiteral("0 commits loaded"), this);
    _loadedLabel->setObjectName(QStringLiteral("commitHistoryLoadedLabel"));
    _loadMoreButton = new QPushButton(QStringLiteral("Load More"), this);
    _loadMoreButton->setObjectName(QStringLiteral("commitHistoryLoadMoreButton"));
    _loadMoreButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    _loadMoreButton->setEnabled(false);

    auto* primaryFilters = new QHBoxLayout;
    primaryFilters->setContentsMargins(4, 4, 4, 0);
    primaryFilters->setSpacing(4);
    primaryFilters->addWidget(_searchEdit, 2);
    primaryFilters->addWidget(_branchCombo, 1);
    primaryFilters->addWidget(_orderCombo);
    primaryFilters->addWidget(viewButton);

    auto* secondaryFilters = new QHBoxLayout;
    secondaryFilters->setContentsMargins(4, 0, 4, 4);
    secondaryFilters->setSpacing(4);
    secondaryFilters->addWidget(_authorEdit, 1);
    secondaryFilters->addWidget(_pathEdit, 1);
    secondaryFilters->addWidget(_fromDateCheck);
    secondaryFilters->addWidget(_fromDateEdit);
    secondaryFilters->addWidget(_toDateCheck);
    secondaryFilters->addWidget(_toDateEdit);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(6, 3, 4, 4);
    footer->addWidget(_loadedLabel);
    footer->addStretch();
    footer->addWidget(_loadMoreButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(primaryFilters);
    layout->addLayout(secondaryFilters);
    layout->addWidget(_view);
    layout->addLayout(footer);

    _queryTimer = new QTimer(this);
    _queryTimer->setSingleShot(true);
    _queryTimer->setInterval(kQueryDebounceMs);

    const auto scheduleTextQuery = [this](const QString&) {
        scheduleQueryChanged();
    };
    connect(_searchEdit, &QLineEdit::textChanged, this, scheduleTextQuery);
    connect(_authorEdit, &QLineEdit::textChanged, this, scheduleTextQuery);
    connect(_pathEdit, &QLineEdit::textChanged, this, scheduleTextQuery);
    connect(_queryTimer, &QTimer::timeout, this, &CommitGraphWidget::emitQueryChanged);
    connect(_branchCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { emitQueryChanged(); });
    connect(_orderCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { emitQueryChanged(); });
    connect(_fromDateCheck, &QCheckBox::toggled, this, [this](bool checked) {
        _fromDateEdit->setEnabled(checked);
        emitQueryChanged();
    });
    connect(_toDateCheck, &QCheckBox::toggled, this, [this](bool checked) {
        _toDateEdit->setEnabled(checked);
        emitQueryChanged();
    });
    connect(_fromDateEdit, &QDateEdit::dateChanged, this, [this](const QDate&) {
        if (_fromDateCheck->isChecked())
            emitQueryChanged();
    });
    connect(_toDateEdit, &QDateEdit::dateChanged, this, [this](const QDate&) {
        if (_toDateCheck->isChecked())
            emitQueryChanged();
    });
    connect(_showGraphAction, &QAction::toggled, this, [this](bool) {
        updateGraphVisibility();
        saveSettings();
    });
    connect(_showRefsAction, &QAction::toggled, this, [this](bool checked) {
        _model->setShowReferences(checked);
        saveSettings();
    });
    connect(_loadMoreButton, &QPushButton::clicked, this, [this] {
        if (!_hasMore || _historyLoading)
            return;
        _historyLoading = true;
        updateFooter();
        emit sigLoadMoreRequested();
    });

    connect(_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &CommitGraphWidget::slotSelectionChanged);

    _view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_view, &QTreeView::customContextMenuRequested,
            this, &CommitGraphWidget::slotContextMenu);

    updateGraphVisibility();
    updateFooter();
}

CommitGraphWidget::~CommitGraphWidget()
{
    saveSettings();
}

void CommitGraphWidget::setCommits(const QVector<Git::Commit>& commits)
{
    Git::CommitHistoryPage page;
    page.commits = commits;
    resetHistory(page);
}

void CommitGraphWidget::resetHistory(const Git::CommitHistoryPage& page)
{
    QString selectedHash;
    const QModelIndexList selected = _view->selectionModel()->selectedRows();
    if (!selected.isEmpty())
        selectedHash = _model->commitAt(selected.first().row()).hash;

    {
        const QSignalBlocker selectionBlocker(_view->selectionModel());
        _model->setCommits(page.commits);
        if (!selectedHash.isEmpty())
            selectCommit(selectedHash);
    }
    _hasMore = page.hasMore;
    _historyLoading = false;
    _queryPending = false;
    ensureGraphColumnWidth();
    updateFooter();
    updateGraphVisibility();

    if (!selectedHash.isEmpty() && _model->rowForHash(selectedHash) < 0)
        emit sigCommitSelectionCleared();
}

void CommitGraphWidget::appendHistory(const Git::CommitHistoryPage& page)
{
    if (page.resetRequired) {
        resetHistory(page);
        return;
    }

    _model->appendCommits(page.commits);
    _hasMore = page.hasMore;
    _historyLoading = false;
    ensureGraphColumnWidth();
    updateFooter();
}

void CommitGraphWidget::setHistoryLoading(bool loading)
{
    _historyLoading = loading;
    updateFooter();
}

void CommitGraphWidget::setBranches(const QVector<Git::Branch>& branches)
{
    const QString previousBranch = _branchCombo->currentData().toString();
    {
        const QSignalBlocker blocker(_branchCombo);
        _branchCombo->clear();
        _branchCombo->addItem(QStringLiteral("Current branch (HEAD)"), QString());
        _branchCombo->addItem(QStringLiteral("All branches"), QStringLiteral("*"));
        if (!branches.isEmpty())
            _branchCombo->insertSeparator(_branchCombo->count());

        QSet<QString> addedRefs;
        for (const Git::Branch& branch : branches) {
            const QString ref = branch.fullName.isEmpty() ? branch.name : branch.fullName;
            if (ref.isEmpty() || addedRefs.contains(ref))
                continue;
            addedRefs.insert(ref);

            QString label = branch.name;
            if (branch.isRemote)
                label = QStringLiteral("Remote: %1").arg(label);
            else if (branch.isCurrent)
                label = QStringLiteral("* %1").arg(label);
            _branchCombo->addItem(label, ref);
            _branchCombo->setItemData(_branchCombo->count() - 1, ref, Qt::ToolTipRole);
        }

        int index = _branchCombo->findData(previousBranch);
        if (index < 0)
            index = 0;
        _branchCombo->setCurrentIndex(index);
    }

    if (_branchCombo->currentData().toString() != previousBranch)
        emitQueryChanged();
}

Git::CommitHistoryQuery CommitGraphWidget::historyQuery() const
{
    Git::CommitHistoryQuery query;
    query.searchText = _searchEdit->text().trimmed();
    query.author = _authorEdit->text().trimmed();
    query.branch = _branchCombo->currentData().toString();
    query.path = _pathEdit->text().trimmed();
    query.oldestFirst = _orderCombo->currentData().toBool();
    query.limit = kHistoryPageSize;

    if (_fromDateCheck->isChecked()) {
        query.fromDate = QDateTime(_fromDateEdit->date(), QTime(0, 0),
                                   Qt::LocalTime).toUTC();
    }
    if (_toDateCheck->isChecked()) {
        query.toDate = QDateTime(_toDateEdit->date(), QTime(23, 59, 59, 999),
                                 Qt::LocalTime).toUTC();
    }
    return query;
}

void CommitGraphWidget::clear()
{
    const bool hadSelection = !_view->selectionModel()->selectedRows().isEmpty();
    {
        const QSignalBlocker selectionBlocker(_view->selectionModel());
        _model->setCommits({});
    }
    _hasMore = false;
    _historyLoading = false;
    _queryPending = false;
    updateFooter();
    if (hadSelection)
        emit sigCommitSelectionCleared();
}

void CommitGraphWidget::selectCommit(const QString& hash)
{
    if (hash.isEmpty()) {
        _view->clearSelection();
        return;
    }

    const int row = _model->rowForHash(hash);
    if (row < 0)
        return;

    const QModelIndex index = _model->index(row, CommitGraphModel::ColMessage);
    _view->setCurrentIndex(index);
    _view->selectionModel()->select(
        index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    _view->scrollTo(index, QAbstractItemView::PositionAtCenter);
}

void CommitGraphWidget::scheduleQueryChanged()
{
    _queryTimer->start();
}

void CommitGraphWidget::emitQueryChanged()
{
    _queryTimer->stop();
    _queryPending = true;
    updateGraphVisibility();
    saveSettings();
    emit sigHistoryQueryChanged(historyQuery());
}

void CommitGraphWidget::updateFooter()
{
    const int count = _model->rowCount();
    QString text = count == 1
        ? QStringLiteral("1 commit loaded")
        : QStringLiteral("%1 commits loaded").arg(count);
    if (_historyLoading)
        text += QStringLiteral(" - loading...");
    else if (_hasMore)
        text += QStringLiteral(" - more available");
    _loadedLabel->setText(text);

    _loadMoreButton->setText(_historyLoading
        ? QStringLiteral("Loading...") : QStringLiteral("Load More"));
    _loadMoreButton->setEnabled(_hasMore && !_historyLoading);
}

void CommitGraphWidget::updateGraphVisibility()
{
    const Git::CommitHistoryQuery query = historyQuery();
    const bool graphIsReliable = !_queryPending
        && !query.oldestFirst && !query.hasFilters();
    _view->setColumnHidden(CommitGraphModel::ColGraph,
                           !_showGraphAction->isChecked() || !graphIsReliable);
}

void CommitGraphWidget::ensureGraphColumnWidth()
{
    const int requiredWidth = qMax(80, (_model->maxLaneCount() + 1) * 16 + 8);
    if (_view->columnWidth(CommitGraphModel::ColGraph) < requiredWidth)
        _view->setColumnWidth(CommitGraphModel::ColGraph, requiredWidth);
}

void CommitGraphWidget::saveSettings() const
{
    if (!_view || !_orderCombo || !_showGraphAction || !_showRefsAction)
        return;

    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue(QStringLiteral("headerState"), _view->header()->saveState());
    settings.setValue(QStringLiteral("oldestFirst"),
                      _orderCombo->currentData().toBool());
    settings.setValue(QStringLiteral("showGraph"), _showGraphAction->isChecked());
    settings.setValue(QStringLiteral("showReferences"), _showRefsAction->isChecked());
    settings.endGroup();
}

void CommitGraphWidget::slotSelectionChanged()
{
    const QModelIndexList selected = _view->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        emit sigCommitSelectionCleared();
        return;
    }

    const int row = selected.first().row();
    if (row >= 0 && row < _model->rowCount())
        emit sigCommitSelected(_model->commitAt(row));
}

void CommitGraphWidget::slotContextMenu(const QPoint& pos)
{
    const QModelIndex index = _view->indexAt(pos);
    if (!index.isValid())
        return;

    const Git::Commit commit = _model->commitAt(index.row());
    const QString hash = commit.hash;

    QMenu menu(this);

    menu.addAction(QStringLiteral("Copy Full Hash"), [hash] {
        QGuiApplication::clipboard()->setText(hash);
    });
    menu.addAction(QStringLiteral("View Commit Details"), [this, hash] {
        emit sigCommitDetailsRequested(hash);
    });

    auto* parentsMenu = menu.addMenu(QStringLiteral("View Parent Commit"));
    if (commit.parents.isEmpty()) {
        QAction* noParent = parentsMenu->addAction(QStringLiteral("No parent commit"));
        noParent->setEnabled(false);
    } else {
        for (const QString& parentHash : commit.parents) {
            parentsMenu->addAction(parentHash.left(8), [this, parentHash] {
                emit sigCommitDetailsRequested(parentHash);
            });
        }
    }

    const QStringList children = _model->childHashes(hash);
    auto* childrenMenu = menu.addMenu(QStringLiteral("View Child Commit"));
    if (children.isEmpty()) {
        QAction* noChild = childrenMenu->addAction(QStringLiteral("No loaded child commit"));
        noChild->setEnabled(false);
    } else {
        for (const QString& childHash : children) {
            const int childRow = _model->rowForHash(childHash);
            QString label = childHash.left(8);
            if (childRow >= 0)
                label += QStringLiteral("  %1").arg(_model->commitAt(childRow).subject);
            childrenMenu->addAction(label, [this, childHash] {
                emit sigCommitDetailsRequested(childHash);
            });
        }
    }

    menu.addSeparator();
    menu.addAction(QStringLiteral("Add tag here…"), [this, hash] {
        emit sigCreateTagRequested(hash);
    });

    // 解析当前提交上的标签，每个标签提供一个删除项
    for (const QString& ref : commit.refs) {
        if (ref.startsWith(QLatin1String("tag: "))) {
            const QString tagName = ref.mid(5);
            menu.addAction(
                QStringLiteral("Delete tag: %1").arg(tagName),
                [this, tagName]() {
                    emit sigDeleteTagRequested(tagName);
                });
        }
    }

    menu.exec(_view->viewport()->mapToGlobal(pos));
}

// ============================================================
// CommitGraphModel
// ============================================================

CommitGraphModel::CommitGraphModel(QObject* parent)
    : QAbstractTableModel(parent)
{}

void CommitGraphModel::setCommits(const QVector<Git::Commit>& commits)
{
    beginResetModel();
    _commits.clear();
    _commits.reserve(commits.size());
    QSet<QString> hashes;
    for (const Git::Commit& commit : commits) {
        if (!commit.hash.isEmpty() && hashes.contains(commit.hash))
            continue;
        _commits.append(commit);
        if (!commit.hash.isEmpty())
            hashes.insert(commit.hash);
    }
    rebuildIndex();
    endResetModel();
}

void CommitGraphModel::appendCommits(const QVector<Git::Commit>& commits)
{
    QVector<Git::Commit> additions;
    additions.reserve(commits.size());
    QSet<QString> pageHashes;
    for (const Git::Commit& commit : commits) {
        if (!commit.hash.isEmpty()
            && (_rowByHash.contains(commit.hash) || pageHashes.contains(commit.hash))) {
            continue;
        }
        additions.append(commit);
        if (!commit.hash.isEmpty())
            pageHashes.insert(commit.hash);
    }
    if (additions.isEmpty())
        return;

    const int firstRow = _commits.size();
    beginInsertRows({}, firstRow, firstRow + additions.size() - 1);
    for (const Git::Commit& commit : additions) {
        const int row = _commits.size();
        _commits.append(commit);
        if (!commit.hash.isEmpty())
            _rowByHash.insert(commit.hash, row);
        for (const QString& parentHash : commit.parents) {
            QStringList& children = _childrenByParent[parentHash];
            if (!children.contains(commit.hash))
                children.append(commit.hash);
        }
        _maxLane = qMax(_maxLane, qMax(commit.lane,
                                      commit.activeLanes.size() - 1));
    }
    endInsertRows();
}

void CommitGraphModel::setShowReferences(bool show)
{
    if (_showReferences == show)
        return;
    _showReferences = show;
    if (!_commits.isEmpty()) {
        emit dataChanged(index(0, ColMessage),
                         index(_commits.size() - 1, ColMessage),
                         {Qt::UserRole});
    }
}

const Git::Commit& CommitGraphModel::commitAt(int row) const
{
    return _commits.at(row);
}

int CommitGraphModel::rowForHash(const QString& hash) const
{
    const auto exact = _rowByHash.constFind(hash);
    if (exact != _rowByHash.cend())
        return exact.value();

    int matchingRow = -1;
    for (auto it = _rowByHash.cbegin(); it != _rowByHash.cend(); ++it) {
        if (!it.key().startsWith(hash, Qt::CaseInsensitive))
            continue;
        if (matchingRow >= 0)
            return -1;
        matchingRow = it.value();
    }
    return matchingRow;
}

QStringList CommitGraphModel::childHashes(const QString& parentHash) const
{
    return _childrenByParent.value(parentHash);
}

void CommitGraphModel::rebuildIndex()
{
    _rowByHash.clear();
    _childrenByParent.clear();
    _maxLane = 0;
    for (int row = 0; row < _commits.size(); ++row) {
        const Git::Commit& commit = _commits.at(row);
        if (!commit.hash.isEmpty())
            _rowByHash.insert(commit.hash, row);
        for (const QString& parentHash : commit.parents) {
            QStringList& children = _childrenByParent[parentHash];
            if (!children.contains(commit.hash))
                children.append(commit.hash);
        }
        _maxLane = qMax(_maxLane, qMax(commit.lane,
                                      commit.activeLanes.size() - 1));
    }
}

int CommitGraphModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : _commits.size();
}

int CommitGraphModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant CommitGraphModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= _commits.size())
        return {};

    const Git::Commit& c = _commits.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColGraph:   return {};  // 由 Delegate 绘制
        case ColHash:    return c.shortHash;
        case ColMessage: return c.subject;  // refs 由 Delegate 以徽章形式绘制
        case ColAuthor:  return c.authorName;
        case ColDate:    return c.date.toLocalTime().toString("yyyy-MM-dd HH:mm");
        default:         return {};
        }
    }

    // 将 refs 列表暴露给 Delegate 用于绘制徽章
    if (role == Qt::UserRole && index.column() == ColMessage)
        return _showReferences ? QVariant(c.refs) : QVariant(QStringList{});

    if (role == Qt::ToolTipRole && index.column() == ColMessage)
        return c.hash;

    return {};
}

QVariant CommitGraphModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColGraph:   return QStringLiteral("Graph");
    case ColHash:    return QStringLiteral("Hash");
    case ColMessage: return QStringLiteral("Message");
    case ColAuthor:  return QStringLiteral("Author");
    case ColDate:    return QStringLiteral("Date");
    default:         return {};
    }
}

// ============================================================
// CommitGraphDelegate
// ============================================================

CommitGraphDelegate::CommitGraphDelegate(CommitGraphModel* model, QObject* parent)
    : QStyledItemDelegate(parent)
    , _model(model)
{}

int CommitGraphDelegate::findLane(const QVector<QString>& lanes, const QString& hash)
{
    for (int i = 0; i < lanes.size(); ++i)
        if (lanes[i] == hash) return i;
    return -1;
}

QSize CommitGraphDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    s.setHeight(qMax(s.height(), kRowHeight));
    return s;
}

void CommitGraphDelegate::paint(QPainter* painter,
                                const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    // Message 列：先绘制背景，再绘制 ref 徽章 + 提交主题文本
    if (index.column() == CommitGraphModel::ColMessage) {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter);

        const QStringList refs = index.data(Qt::UserRole).toStringList();
        const Git::Commit& commit = _model->commitAt(index.row());
        const QString subject = index.data(Qt::DisplayRole).toString();

        painter->save();

        const QFontMetrics fm(option.font);
        constexpr int kBadgePad = 5;  ///< 徽章内水平留白
        constexpr int kBadgeGap = 3;  ///< 徽章间距
        const int     badgeH    = qMin(16, option.rect.height() - 4);
        const int     cy        = option.rect.center().y();
        int x = option.rect.left() + 4;

        struct Badge { QString label; QColor bg; };
        QVector<Badge> badges;
        badges.reserve(refs.size() * 2);

        for (const QString& ref : refs) {
            if (ref.startsWith(QLatin1String("HEAD -> "))) {
                badges.append({"HEAD",     QColor("#da3633")});  // 红色
                badges.append({ref.mid(8), QColor("#238636")});  // 绿色（本地分支）
            } else if (ref.startsWith(QLatin1String("tag: "))) {
                badges.append({ref.mid(5), QColor("#9e6a03")});  // 金色（标签）
            } else if (commit.remoteRefs.contains(ref)) {
                badges.append({ref, QColor("#1f6feb")});          // 蓝色（远端分支）
            } else {
                badges.append({ref, QColor("#238636")});          // 绿色（本地分支）
            }
        }

        painter->setRenderHint(QPainter::Antialiasing);
        for (const Badge& badge : std::as_const(badges)) {
            const int    textW    = fm.horizontalAdvance(badge.label);
            const int    badgeW   = textW + kBadgePad * 2;
            const QRect  badgeRect(x, cy - badgeH / 2, badgeW, badgeH);

            painter->setPen(Qt::NoPen);
            painter->setBrush(badge.bg);
            painter->drawRoundedRect(badgeRect, 3, 3);

            painter->setPen(Qt::white);
            painter->setFont(option.font);
            painter->drawText(badgeRect, Qt::AlignCenter, badge.label);

            x += badgeW + kBadgeGap;
        }

        if (!badges.isEmpty())
            x += 2;  // subject 与最后一个徽章之间额外留白

        const QColor textColor = option.palette.color(
            (option.state & QStyle::State_Selected)
                ? QPalette::HighlightedText : QPalette::Text);
        painter->setPen(textColor);
        painter->setFont(option.font);
        painter->drawText(
            QRect(x, option.rect.top(), option.rect.right() - x, option.rect.height()),
            Qt::AlignVCenter | Qt::TextSingleLine,
            subject);

        painter->restore();
        return;
    }

    // 非图列交给默认绘制
    if (index.column() != CommitGraphModel::ColGraph) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    // 先绘制默认背景（选中高亮等）
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear(); // 不绘制文字
    QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter);

    const int row = index.row();
    if (row >= _model->rowCount())
        return;

    const Git::Commit& commit  = _model->commitAt(row);
    const QVector<QString>& prevLanes =
        (row > 0) ? _model->commitAt(row - 1).activeLanes : QVector<QString>{};
    const QVector<QString>& currLanes = commit.activeLanes;

    const int left = option.rect.left();
    const int cy   = option.rect.center().y();
    const int top  = option.rect.top();
    const int bot  = option.rect.bottom();

    auto lx = [left](int lane) { return laneX(lane, left); };

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const int maxLanes = qMax(prevLanes.size(), currLanes.size());

    // ---- 1. 直通线（与当前提交无交互的 lane）----
    for (int i = 0; i < maxLanes; ++i) {
        const QString prevH = (i < prevLanes.size()) ? prevLanes[i] : QString{};
        const QString currH = (i < currLanes.size()) ? currLanes[i] : QString{};

        if (prevH.isEmpty() && currH.isEmpty())
            continue;
        if (i == commit.lane)
            continue; // 提交所在列单独处理
        if (!prevH.isEmpty() && prevH == commit.hash)
            continue; // 汇入线单独处理

        QPen pen(Git::laneColor(i), 1.5f);
        painter->setPen(pen);

        if (!prevH.isEmpty() && !currH.isEmpty()) {
            // 上下均有：直线穿过
            painter->drawLine(lx(i), top, lx(i), bot);
        } else if (prevH.isEmpty() && !currH.isEmpty()) {
            // 新 lane 在此行出现。
            // 若该 lane 是本提交的父提交，对角线由 outgoing 负责，
            // 此处不再重复画竖线（避免与对角线叠加产生视觉噪音）。
            if (!commit.parents.contains(currH)) {
                painter->drawLine(lx(i), cy, lx(i), bot);
            }
        } else {
            // 上有下无：lane 在此行结束（不太可能发生，保险起见画上半）
            painter->drawLine(lx(i), top, lx(i), cy);
        }
    }

    // ---- 2. 汇入线（prev 中追踪此提交的 lane -> 节点）----
    for (int i = 0; i < prevLanes.size(); ++i) {
        if (prevLanes[i] != commit.hash)
            continue;
        QPen pen(Git::laneColor(i), 1.5f);
        painter->setPen(pen);
        painter->drawLine(lx(i), top, lx(commit.lane), cy);
    }

    // ---- 3. 节点到父提交的连线 ----
    for (int p = 0; p < commit.parents.size(); ++p) {
        const int pLane = findLane(currLanes, commit.parents[p]);
        if (pLane < 0)
            continue;
        QPen pen(Git::laneColor(pLane), 1.5f);
        painter->setPen(pen);
        painter->drawLine(lx(commit.lane), cy, lx(pLane), bot);
    }

    // 若无父提交（历史终点），也无需向下绘线

    // ---- 4. 提交节点圆圈 ----
    {
        const QColor color = Git::laneColor(commit.lane);
        painter->setPen(QPen(color.darker(150), 1.5f));
        painter->setBrush(color);
        painter->drawEllipse(QPoint(lx(commit.lane), cy), kNodeRadius, kNodeRadius);
    }

    painter->restore();
}

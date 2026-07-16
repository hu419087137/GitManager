#include "CommitGraphWidget.h"
#include <QPainter>
#include <QApplication>
#include <QFontMetrics>
#include <QHeaderView>
#include <QMenu>
#include <QVBoxLayout>
#include <QItemSelectionModel>

// ============================================================
// CommitGraphWidget
// ============================================================

CommitGraphWidget::CommitGraphWidget(QWidget* parent)
    : QWidget(parent)
{
    _model    = new CommitGraphModel(this);
    _delegate = new CommitGraphDelegate(_model, this);

    _view = new QTreeView(this);
    _view->setModel(_model);
    _view->setItemDelegate(_delegate);
    _view->setRootIsDecorated(false);
    _view->setAlternatingRowColors(true);
    _view->setSelectionBehavior(QAbstractItemView::SelectRows);
    _view->setSelectionMode(QAbstractItemView::SingleSelection);
    _view->setUniformRowHeights(true);
    _view->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 列宽初始值
    _view->header()->setStretchLastSection(false);
    _view->header()->setSectionResizeMode(CommitGraphModel::ColGraph,   QHeaderView::Interactive);
    _view->header()->setSectionResizeMode(CommitGraphModel::ColHash,    QHeaderView::Interactive);
    _view->header()->setSectionResizeMode(CommitGraphModel::ColMessage, QHeaderView::Stretch);
    _view->header()->setSectionResizeMode(CommitGraphModel::ColAuthor,  QHeaderView::Interactive);
    _view->header()->setSectionResizeMode(CommitGraphModel::ColDate,    QHeaderView::Interactive);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(_view);

    connect(_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &CommitGraphWidget::slotSelectionChanged);

    _view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_view, &QTreeView::customContextMenuRequested,
            this, &CommitGraphWidget::slotContextMenu);
}

void CommitGraphWidget::setCommits(const QVector<Git::Commit>& commits)
{
    _model->setCommits(commits);

    // 根据 lane 数量设置图列宽
    const int graphColWidth = qMax(80, (_model->maxLaneCount() + 1) * 16 + 8);
    _view->setColumnWidth(CommitGraphModel::ColGraph,  graphColWidth);
    _view->setColumnWidth(CommitGraphModel::ColHash,   70);
    _view->setColumnWidth(CommitGraphModel::ColAuthor, 120);
    _view->setColumnWidth(CommitGraphModel::ColDate,   145);
}

void CommitGraphWidget::clear()
{
    _model->setCommits({});
}

void CommitGraphWidget::slotSelectionChanged()
{
    const QModelIndexList selected = _view->selectionModel()->selectedRows();
    if (selected.isEmpty())
        return;

    const int row = selected.first().row();
    if (row >= 0 && row < _model->rowCount())
        emit sigCommitSelected(_model->commitAt(row));
}

void CommitGraphWidget::slotContextMenu(const QPoint& pos)
{
    const QModelIndex index = _view->indexAt(pos);
    if (!index.isValid())
        return;

    const Git::Commit& commit = _model->commitAt(index.row());

    QMenu menu(this);

    menu.addAction(QStringLiteral("Add tag here…"), [this, &commit]() {
        emit sigCreateTagRequested(commit.hash);
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
    _commits = commits;
    _maxLane = 0;
    for (const Git::Commit& c : _commits)
        _maxLane = qMax(_maxLane, c.lane);
    endResetModel();
}

const Git::Commit& CommitGraphModel::commitAt(int row) const
{
    return _commits.at(row);
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
        return QVariant(c.refs);

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

        const QStringList refs   = index.data(Qt::UserRole).toStringList();
        const QString     subject = index.data(Qt::DisplayRole).toString();

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
            } else if (ref.contains(QLatin1Char('/'))) {
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

    auto lx = [&](int lane) { return laneX(lane, left); };

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

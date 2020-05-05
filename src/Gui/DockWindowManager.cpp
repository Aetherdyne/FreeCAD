/***************************************************************************
 *   Copyright (c) 2007 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include "PreCompiled.h"
#ifndef _PreComp_
# include <QPointer>
# include <QPainter>
# include <QDockWidget>
# include <QMdiArea>
# include <QTabBar>
# include <QTreeView>
# include <QHeaderView>
# include <QToolTip>
# include <QAction>
# include <QKeyEvent>
# include <QMap>
# include <QTextStream>
# include <QComboBox>
# include <QBoxLayout>
# include <QSpacerItem>
# include <QSplitter>
#endif

#include <array>

#include <Base/Tools.h>
#include <Base/Console.h>
#include "DockWindowManager.h"
#include "MainWindow.h"
#include "ViewParams.h"
#include "View3DInventor.h"
#include "SplitView3DInventor.h"
#include "Application.h"
#include "Control.h"
#include <App/Application.h>
#include "propertyeditor/PropertyEditor.h"

FC_LOG_LEVEL_INIT("Dock", true, true);

using namespace Gui;

DockWindowItems::DockWindowItems()
{
}

DockWindowItems::~DockWindowItems()
{
}

void DockWindowItems::addDockWidget(const char* name, Qt::DockWidgetArea pos, bool visibility, bool tabbed)
{
    DockWindowItem item;
    item.name = QString::fromLatin1(name);
    item.pos = pos;
    item.visibility = visibility;
    item.tabbed = tabbed;
    _items << item;
}

void DockWindowItems::setDockingArea(const char* name, Qt::DockWidgetArea pos)
{
    for (QList<DockWindowItem>::iterator it = _items.begin(); it != _items.end(); ++it) {
        if (it->name == QLatin1String(name)) {
            it->pos = pos;
            break;
        }
    }
}

void DockWindowItems::setVisibility(const char* name, bool v)
{
    for (QList<DockWindowItem>::iterator it = _items.begin(); it != _items.end(); ++it) {
        if (it->name == QLatin1String(name)) {
            it->visibility = v;
            break;
        }
    }
}

void DockWindowItems::setVisibility(bool v)
{
    for (QList<DockWindowItem>::iterator it = _items.begin(); it != _items.end(); ++it) {
        it->visibility = v;
    }
}

const QList<DockWindowItem>& DockWindowItems::dockWidgets() const
{
    return this->_items;
}

// -----------------------------------------------------------

#ifdef FC_HAS_DOCK_OVERLAY

static const int _TitleButtonSize = 12;

#define TITLE_BUTTON_COLOR "# c #101010"

static const char *_PixmapOverlay[]={
    "10 10 2 1",
    ". c None",
    TITLE_BUTTON_COLOR,
    "##########",
    "#........#",
    "#........#",
    "##########",
    "#........#",
    "#........#",
    "#........#",
    "#........#",
    "#........#",
    "##########",
};

// -----------------------------------------------------------

OverlayProxyWidget::OverlayProxyWidget(OverlayTabWidget *tabOverlay)
    :QWidget(tabOverlay->parentWidget()), owner(tabOverlay)
{
    pos = owner->getDockArea();
}

void OverlayProxyWidget::enterEvent(QEvent *)
{
    if(!owner->count())
        return;
    drawLine = true;
    update();
    if(ViewParams::getDockOverlayActivateOnHover())
        DockWindowManager::instance()->refreshOverlay();
}

void OverlayProxyWidget::leaveEvent(QEvent *)
{
    drawLine = false;
    update();
}

void OverlayProxyWidget::hideEvent(QHideEvent *)
{
    drawLine = false;
    update();
}

void OverlayProxyWidget::mousePressEvent(QMouseEvent *ev)
{
    if(!owner->count() || ev->button() != Qt::LeftButton)
        return;

    DockWindowManager::instance()->refreshOverlay(this);
}

void OverlayProxyWidget::paintEvent(QPaintEvent *)
{
    if(!drawLine)
        return;
    QPainter painter(this);
    painter.setPen(QPen(Qt::black, 2));
    QSize s = this->size();
    switch(pos) {
    case Qt::LeftDockWidgetArea:
        painter.drawLine(s.width()-2, 0, s.width()-2, s.height());
        break;
    case Qt::RightDockWidgetArea:
        painter.drawLine(1, 0, 1, s.height()-2);
        break;
    case Qt::TopDockWidgetArea:
        painter.drawLine(0, s.height()-2, s.width(), s.height()-2);
        break;
    case Qt::BottomDockWidgetArea:
        painter.drawLine(0, 1, s.width(), 1);
        break;
    }
}

OverlayToolButton::OverlayToolButton(QWidget *parent)
    :QToolButton(parent)
{}

OverlayTabWidget::OverlayTabWidget(QWidget *parent, Qt::DockWidgetArea pos)
    :QTabWidget(parent), dockArea(pos)
{
    // This is necessary to capture any focus lost from switching the tab,
    // otherwise the lost focus will leak to the parent, i.e. MdiArea, which may
    // cause unexpected Mdi sub window switching.
    setFocusPolicy(Qt::StrongFocus);

    splitter = new QSplitter(this);

    switch(pos) {
    case Qt::LeftDockWidgetArea:
        setTabPosition(QTabWidget::West);
        splitter->setOrientation(Qt::Vertical);
        break;
    case Qt::RightDockWidgetArea:
        setTabPosition(QTabWidget::East);
        splitter->setOrientation(Qt::Vertical);
        break;
    case Qt::TopDockWidgetArea:
        setTabPosition(QTabWidget::North);
        splitter->setOrientation(Qt::Horizontal);
        break;
    case Qt::BottomDockWidgetArea:
        setTabPosition(QTabWidget::South);
        splitter->setOrientation(Qt::Horizontal);
        break;
    default:
        break;
    }

    proxyWidget = new OverlayProxyWidget(this);
    proxyWidget->hide();
    _setOverlayMode(proxyWidget,true);

    setOverlayMode(true);
    hide();

    static QIcon pxTransparent;
    if(pxTransparent.isNull()) {
        const char * const bytes[]={
            "10 10 2 1",
            ". c None",
            TITLE_BUTTON_COLOR,
            "..........",
            "...####...",
            ".##....##.",
            "##..##..##",
            "#..####..#",
            "#..####..#",
            "##..##..##",
            ".##....##.",
            "...####...",
            "..........",
        };
        pxTransparent = QIcon(QPixmap(bytes));
    }
    actTransparent.setIcon(pxTransparent);
    actTransparent.setCheckable(true);
    actTransparent.setData(QString::fromLatin1("OBTN Transparent"));
    actTransparent.setParent(this);
    addAction(&actTransparent);

    QPixmap pxAutoHide;
    if(pxAutoHide.isNull()) {
        const char * const bytes[]={
            "10 10 2 1",
            ". c None",
            TITLE_BUTTON_COLOR,
            "...#######",
            ".........#",
            "..##.....#",
            ".##......#",
            "#######..#",
            "#######..#",
            ".##......#",
            "..##.....#",
            ".........#",
            "...#######",
        };
        pxAutoHide = QPixmap(bytes);
    }
    switch(dockArea) {
    case Qt::LeftDockWidgetArea:
        actAutoHide.setIcon(pxAutoHide);
        break;
    case Qt::RightDockWidgetArea:
        actAutoHide.setIcon(pxAutoHide.transformed(QTransform().scale(-1,1)));
        break;
    case Qt::TopDockWidgetArea:
        actAutoHide.setIcon(pxAutoHide.transformed(QTransform().rotate(90)));
        break;
    case Qt::BottomDockWidgetArea:
        actAutoHide.setIcon(pxAutoHide.transformed(QTransform().rotate(-90)));
        break;
    default:
        break;
    }
    actAutoHide.setCheckable(true);
    actAutoHide.setData(QString::fromLatin1("OBTN AutoHide"));
    actAutoHide.setParent(this);
    addAction(&actAutoHide);

    static QIcon pxEditHide;
    if(pxEditHide.isNull()) {
        const char * const bytes[]={
            "10 10 2 1",
            ". c None",
            TITLE_BUTTON_COLOR,
            "##....##..",
            "###..#.##.",
            ".####..###",
            "..###.#..#",
            "..####..#.",
            ".#..####..",
            "##...###..",
            "##...####.",
            "#####..###",
            "####....##",
        };
        pxEditHide = QIcon(QPixmap(bytes));
    }
    actEditHide.setIcon(pxEditHide);
    actEditHide.setCheckable(true);
    actEditHide.setData(QString::fromLatin1("OBTN EditHide"));
    actEditHide.setParent(this);
    addAction(&actEditHide);

    static QIcon pxEditShow;
    if(pxEditShow.isNull()) {
        const char * const bytes[]={
            "10 10 2 1",
            ". c None",
            TITLE_BUTTON_COLOR,
            "......##..",
            ".....#.##.",
            "....#..###",
            "...#..#..#",
            "..##.#..#.",
            ".#.##..#..",
            "##..###...",
            "##...#....",
            "#####.....",
            "####......",
        };
        pxEditShow = QIcon(QPixmap(bytes));
    }
    actEditShow.setIcon(pxEditShow);
    actEditShow.setCheckable(true);
    actEditShow.setData(QString::fromLatin1("OBTN EditShow"));
    actEditShow.setParent(this);
    addAction(&actEditShow);

    static QIcon pxIncrease;
    if(pxIncrease.isNull()) {
        const char * const bytes[]={
            "10 10 2 1",
            ". c None",
            TITLE_BUTTON_COLOR,
            "....##....",
            "....##....",
            "....##....",
            "....##....",
            "##########",
            "##########",
            "....##....",
            "....##....",
            "....##....",
            "....##....",
        };
        pxIncrease = QIcon(QPixmap(bytes));
    }
    actIncrease.setIcon(pxIncrease);
    actIncrease.setData(QString::fromLatin1("OBTN Increase"));
    actIncrease.setParent(this);
    addAction(&actIncrease);

    static QIcon pxDecrease;
    if(pxDecrease.isNull()) {
        const char * const bytes[]={
            "10 10 2 1",
            ". c None",
            TITLE_BUTTON_COLOR,
            "..........",
            "..........",
            "..........",
            "..........",
            "##########",
            "##########",
            "..........",
            "..........",
            "..........",
            "..........",
        };
        pxDecrease = QIcon(QPixmap(bytes));
    }
    actDecrease.setIcon(pxDecrease);
    actDecrease.setData(QString::fromLatin1("OBTN Decrease"));
    actDecrease.setParent(this);
    addAction(&actDecrease);

    actOverlay.setIcon(QPixmap(_PixmapOverlay));
    actOverlay.setData(QString::fromLatin1("OBTN Overlay"));
    actOverlay.setParent(this);
    addAction(&actOverlay);

    retranslate();

    connect(this, SIGNAL(currentChanged(int)), this, SLOT(onCurrentChanged(int)));
    connect(tabBar(), SIGNAL(tabMoved(int,int)), this, SLOT(onTabMoved(int,int)));
    tabBar()->installEventFilter(this);
    connect(splitter, SIGNAL(splitterMoved(int,int)), this, SLOT(onSplitterMoved()));

    timer.setSingleShot(true);
    connect(&timer, SIGNAL(timeout()), this, SLOT(setupLayout()));
}

bool OverlayTabWidget::eventFilter(QObject *o, QEvent *ev)
{
    if(ev->type() == QEvent::Resize && o == tabBar())
        timer.start(10);
    return QTabWidget::eventFilter(o, ev);
}

void OverlayTabWidget::restore(ParameterGrp::handle handle)
{
    std::string widgets = handle->GetASCII("Widgets","");
    for(auto &name : QString::fromLatin1(widgets.c_str()).split(QLatin1Char(','))) {
        if(name.isEmpty())
            continue;
        auto dock = getMainWindow()->findChild<QDockWidget*>(name);
        if(dock)
            addWidget(dock, dock->windowTitle());
    }
    int width = handle->GetInt("Width", 0);
    int height = handle->GetInt("Height", 0);
    int offset1 = handle->GetInt("Offset1", 0);
    int offset2 = handle->GetInt("Offset3", 0);
    setOffset(QSize(offset1,offset2));
    setSizeDelta(handle->GetInt("Offset2", 0));
    if(width && height) {
        QRect rect = geometry();
        setRect(QRect(rect.left(),rect.top(),width,height));
    }
    setAutoHide(handle->GetBool("AutoHide", false));
    setTransparent(handle->GetBool("Transparent", false));
    setEditHide(handle->GetBool("EditHide", false));
    setEditShow(handle->GetBool("EditShow", false));

    std::string savedSizes = handle->GetASCII("Sizes","");
    QList<int> sizes;
    for(auto &size : QString::fromLatin1(savedSizes.c_str()).split(QLatin1Char(',')))
        sizes.append(size.toInt());

    getSplitter()->setSizes(sizes);
    hGrp = handle;
}

void OverlayTabWidget::saveTabs()
{
    if(!hGrp)
        return;

    std::ostringstream os;
    for(int i=0,c=count(); i<c; ++i) {
        auto dock = dockWidget(i);
        if(dock && dock->objectName().size())
            os << dock->objectName().toLatin1().constData() << ",";
    }
    hGrp->SetASCII("Widgets", os.str().c_str());

    if(splitter->isVisible()) {
        os.str("");
        for(int size : splitter->sizes())
            os << size << ",";
        hGrp->SetASCII("Sizes", os.str().c_str());
    }
}

void OverlayTabWidget::onTabMoved(int from, int to)
{
    QWidget *w = splitter->widget(from);
    splitter->insertWidget(to,w);
    saveTabs();
}

void OverlayTabWidget::setTitleBar(QWidget *w)
{
    titleBar = w;
}

void OverlayTabWidget::changeEvent(QEvent *e)
{
    QTabWidget::changeEvent(e);
    if (e->type() == QEvent::LanguageChange)
        retranslate();
}

void OverlayTabWidget::retranslate()
{
    actTransparent.setToolTip(tr("Toggle transparent mode"));
    actAutoHide.setToolTip(tr("Toggle auto hide mode"));
    actEditHide.setToolTip(tr("Toggle auto hide on edit mode"));
    actEditShow.setToolTip(tr("Toggle auto show on edit mode"));
    actIncrease.setToolTip(tr("Increase window size, either width or height depending on the docking site.\n"
                              "Hold ALT key while pressing the button to change size in the other dimension.\n"
                              "Hold SHIFT key while pressing the button to move the window.\n"
                              "Hold ALT + SHIFT key to move the window in the other direction."));
    actDecrease.setToolTip(tr("Decrease window size, either width or height depending on the docking site.\n"
                              "Hold ALT key while pressing to change size in the other dimension.\n"
                              "Hold SHIFT key while pressing the button to move the window.\n"
                              "Hold ALT + SHIFT key to move the window in the other direction."));
    actOverlay.setToolTip(tr("Toggle overlay"));
}

void OverlayTabWidget::onAction(QAction *action)
{
    if(action == &actEditHide) {
        if(action->isChecked()) {
            setAutoHide(false);
            setEditShow(false);
        }
    } else if(action == &actAutoHide) {
        if(action->isChecked()) {
            setEditHide(false);
            setEditShow(false);
        }
    } else if(action == &actEditShow) {
        if(action->isChecked()) {
            setEditHide(false);
            setAutoHide(false);
        }
    } else if(action == &actIncrease)
        changeSize(5);
    else if(action == &actDecrease)
        changeSize(-5);
    else if(action == &actOverlay) {
        DockWindowManager::instance()->setOverlayMode(DockWindowManager::ToggleActive);
        return;
    }
    DockWindowManager::instance()->refreshOverlay(this);
}

bool OverlayTabWidget::checkAutoHide() const
{
    if(isAutoHide())
        return true;

    if(ViewParams::getDockOverlayAutoView()) {
        auto view = getMainWindow()->activeWindow();
        if(!view || (!view->isDerivedFrom(View3DInventor::getClassTypeId())
                        && !view->isDerivedFrom(SplitView3DInventor::getClassTypeId())))
            return true;
    }

    if(isEditShow())
        return !Application::Instance->editDocument() && !Control().activeDialog();

    if(isEditHide() && Application::Instance->editDocument())
        return true;

    return false;
}

static inline OverlayTabWidget *findTabWidget(QWidget *widget=nullptr)
{
    if(!widget)
        widget = qApp->focusWidget();
    for(auto w=widget; w; w=w->parentWidget()) {
        auto dock = qobject_cast<OverlayTabWidget*>(w);
        if(dock)
            return dock;
        auto proxy = qobject_cast<OverlayProxyWidget*>(w);
        if(proxy)
            return proxy->getOwner();
    }
    return nullptr;
}

void OverlayTabWidget::leaveEvent(QEvent*)
{
    DockWindowManager::instance()->refreshOverlay();
}

void OverlayTabWidget::enterEvent(QEvent*)
{
    revealTime = QTime();
    DockWindowManager::instance()->refreshOverlay();
}

void OverlayTabWidget::setRevealTime(const QTime &time)
{
    revealTime = time;
}

class OverlayStyleSheet: public ParameterGrp::ObserverType {
public:

    OverlayStyleSheet() {
        handle = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/MainWindow");
        update();
        handle->Attach(this);
    }

    static OverlayStyleSheet *instance() {
        static OverlayStyleSheet *inst;
        if(!inst)
            inst = new OverlayStyleSheet;
        return inst;
    }

    void OnChange(Base::Subject<const char*> &, const char* sReason) {
        if(!sReason)
            return;
        if(strcmp(sReason, "StyleSheet")==0
                || strcmp(sReason, "OverlayActiveStyleSheet")==0
                || strcmp(sReason, "OverlayOnStyleSheet")==0
                || strcmp(sReason, "OverlayOffStyleSheet")==0)
        {
            DockWindowManager::instance()->refreshOverlay(nullptr, true);
        }
    }

    void update() {
        QString mainstyle = QString::fromLatin1(handle->GetASCII("StyleSheet").c_str());

        QLatin1String prefix("qss:");
        QString name;

        onStyleSheet.clear();
        if(ViewParams::getDockOverlayExtraState()) {
            name = QString::fromLatin1("%1.overlay").arg(mainstyle);
            if(!QFile::exists(name)) {
                name = prefix + name;
                if(!QFile::exists(name)) {
                    name = QString::fromUtf8(handle->GetASCII("OverlayOnStyleSheet").c_str());
                    if(!QFile::exists(name))
                        name = prefix + name;
                }
            }

            if(QFile::exists(name)) {
                QFile f(name);
                if(f.open(QFile::ReadOnly)) {
                    QTextStream str(&f);
                    onStyleSheet = str.readAll();
                }
            }
            if(onStyleSheet.isEmpty()) {
                static QLatin1String _default(
                    "* { background-color: transparent; border: 1px solid palette(dark); alternate-background-color: transparent;}"
                    "QTreeWidget, QListWidget { background: palette(base) }"
                    "QToolTip { background-color: palette(base) }"

                    // Both background and border are necessary to make this work.
                    // And this spare us to have to call QTabWidget::setDocumentMode(true).
                    "QTabWidget:pane { background-color: transparent; border: transparent }"
                );
                onStyleSheet = _default;
            }
        }

        name = QString::fromLatin1("%1.overlay2").arg(mainstyle);
        if(!QFile::exists(name)) {
            name = prefix + name;
            if(!QFile::exists(name)) {
                name = QString::fromUtf8(handle->GetASCII("OverlayOffStyleSheet").c_str());
                if(!QFile::exists(name))
                    name = prefix + name;
            }
        }
        offStyleSheet.clear();
        if(QFile::exists(name)) {
            QFile f(name);
            if(f.open(QFile::ReadOnly)) {
                QTextStream str(&f);
                offStyleSheet = str.readAll();
            }
        }
        if(offStyleSheet.isEmpty()) {
            static QLatin1String _default(
                "Gui--OverlayToolButton { background: transparent; padding: 0px; border: none }"
                "Gui--OverlayToolButton:hover { background: palette(light); border: 1px solid palette(dark) }"
                "Gui--OverlayToolButton:focus { background: palette(dark); border: 1px solid palette(dark) }"
                "Gui--OverlayToolButton:pressed { background: palette(dark); border: 1px inset palette(dark) }"
                "Gui--OverlayToolButton:checked { background: palette(dark); border: 1px inset palette(dark) }"
                "Gui--OverlayToolButton:checked:hover { background: palette(light); border: 1px inset palette(dark) }"
            );
            offStyleSheet = _default;
        }

        name = QString::fromLatin1("%1.overlay3").arg(mainstyle);
        if(!QFile::exists(name)) {
            name = prefix + name;
            if(!QFile::exists(name)) {
                name = QString::fromUtf8(handle->GetASCII("OverlayActiveStyleSheet").c_str());
                if(!QFile::exists(name))
                    name = prefix + name;
            }
        }
        activeStyleSheet.clear();
        if(QFile::exists(name)) {
            QFile f(name);
            if(f.open(QFile::ReadOnly)) {
                QTextStream str(&f);
                activeStyleSheet = str.readAll();
            }
        }
        if(activeStyleSheet.isEmpty()) {
            static QLatin1String _default(
                "* { background-color: transparent;"
                    "color: palette(window-text);"
                    "border: 1px solid palette(dark);"
                    "alternate-background-color: transparent;}"
                "QComboBox { background : palette(base);"
                            "selection-background-color: palette(highlight);}"
                "QComboBox:editable { background : palette(base);}"
                "QComboBox:!editable { background : palette(base);}"
                "QLineEdit { background : palette(base);}"
                "QAbstractSpinBox { background : palette(base);}"
                "QTabWidget:pane { background-color: transparent; border: transparent }"
                "QTabBar { background: transparent; border: none;}"
                "QTabBar::tab {color: #1a1a1a;"
                              "background-color: transparent;"
                              "border: 1px solid palette(dark);"
                              "padding: 5px}"
                "QTabBar::tab:selected {color: palette(text); background-color: #aaaaaaaa;}"
                "QTabBar::tab:hover {color: palette(text); background-color: palette(light);}"
                "QHeaderView::section {background-color: transparent; border: 1px solid palette(dark); padding: 1px}"
                "QTreeWidget, QListWidget {background: palette(base)}" // necessary for checkable item to work in linux
                "QToolTip {background-color: palette(base);}"
                "Gui--CallTipsList::item { background-color: palette(base);}"
                "Gui--CallTipsList::item::selected { background-color: palette(highlight);}"
                "QDialog { background-color: palette(window); }"
                "QAbstractButton { background: palette(window);"
                                  "padding: 2px 4px;"
                                  "border: 1px solid palette(dark) }"
                "QAbstractButton:hover { background: palette(light); border: 1px solid palette(dark) }"
                "QAbstractButton:focus { background: palette(dark) ; border: 1px solid palette(dark)}"
                "QAbstractButton:pressed { background: palette(dark); border: 1px inset palette(dark) }"
                "QAbstractButton:checked { background: palette(dark); border: 1px inset palette(dark) }"
                "QAbstractButton:checked:hover { background: palette(light); border: 1px inset palette(dark) }"
                "Gui--OverlayToolButton { background: transparent; padding: 0px; border: none }"
                "QMenu, QMenu::item { color: palette(text); background-color: palette(window) }"
                "QMenu::item::selected,"
                "QMenu::item::pressed { color: palette(highlighted-text); background-color: palette(highlight)}"
                "QMenu::item::disabled { color: palette(mid) }"
                );
            activeStyleSheet = _default;
        }

        if(onStyleSheet.isEmpty()) {
            onStyleSheet = activeStyleSheet;
            hideTab = false;
            hideScrollBar = false;
            hideHeader = false;
        } else {
            hideTab = (onStyleSheet.indexOf(QLatin1String("QTabBar")) < 0);
            hideScrollBar = (onStyleSheet.indexOf(QLatin1String("QAbstractScrollArea")) < 0);
            hideHeader = (onStyleSheet.indexOf(QLatin1String("QHeaderView")) < 0);
        }
    }

    ParameterGrp::handle handle;
    QString onStyleSheet;
    QString offStyleSheet;
    QString activeStyleSheet;
    bool hideTab = false;
    bool hideHeader = false;
    bool hideScrollBar = false;
};

void OverlayTabWidget::_setOverlayMode(QWidget *widget, int enable)
{
    if(!widget)
        return;

#if QT_VERSION>QT_VERSION_CHECK(5,12,2) && QT_VERSION < QT_VERSION_CHECK(5,12,6)
    // Work around Qt bug https://bugreports.qt.io/browse/QTBUG-77006
    if(enable < 0)
        widget->setStyleSheet(OverlayStyleSheet::instance()->activeStyleSheet);
    else if(enable)
        widget->setStyleSheet(OverlayStyleSheet::instance()->onStyleSheet);
    else
        widget->setStyleSheet(OverlayStyleSheet::instance()->offStyleSheet);
#endif

    auto tabbar = qobject_cast<QTabBar*>(widget);
    if(tabbar) {
        // Stylesheet QTabWidget::pane make the following two calls unnecessary
        //
        // tabbar->setDrawBase(enable>0);
        // tabbar->setDocumentMode(enable!=0);

        if(!tabbar->autoHide() || tabbar->count()>1) {
            if(!OverlayStyleSheet::instance()->hideTab)
                tabbar->setVisible(true);
            else
                tabbar->setVisible(enable==0 || (enable<0 && tabbar->count()>1));
            return;
        }
    }
    if(enable!=0) {
        widget->setWindowFlags(widget->windowFlags() & Qt::FramelessWindowHint);
    } else {
        widget->setWindowFlags(widget->windowFlags() & ~Qt::FramelessWindowHint);
    }
    widget->setAttribute(Qt::WA_NoSystemBackground, enable!=0);
    widget->setAttribute(Qt::WA_TranslucentBackground, enable!=0);

    auto scrollarea = qobject_cast<QAbstractScrollArea*>(widget);
    if(scrollarea) {
        if(enable>0 && OverlayStyleSheet::instance()->hideScrollBar) {
            scrollarea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            scrollarea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        } else {
            scrollarea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            scrollarea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
    }

    auto treeview = qobject_cast<QTreeView*>(widget);
    if(treeview) {
        if(treeview->header()) 
            treeview->header()->setVisible(
                    !OverlayStyleSheet::instance()->hideScrollBar || enable<=0);
    }
}

void OverlayTabWidget::setOverlayMode(QWidget *widget, int enable)
{
    if(!widget)
        return;
    _setOverlayMode(widget, enable);

    if(qobject_cast<QComboBox*>(widget)) {
        // do not set child QAbstractItemView of QComboBox, otherwise the drop down box
        // won't be shown
        return;
    }
    for(auto child : widget->children())
        setOverlayMode(qobject_cast<QWidget*>(child), enable);
}

void OverlayTabWidget::setAutoHide(bool enable)
{
    if(actAutoHide.isChecked() == enable)
        return;
    if(hGrp)
        hGrp->SetBool("AutoHide", enable);
    actAutoHide.setChecked(enable);
    if(enable) {
        setEditHide(false);
        setEditShow(false);
    }
    DockWindowManager::instance()->refreshOverlay(this);
}

void OverlayTabWidget::setTransparent(bool enable)
{
    if(actTransparent.isChecked() == enable)
        return;
    if(hGrp)
        hGrp->SetBool("Transparent", enable);
    actTransparent.setChecked(enable);
    DockWindowManager::instance()->refreshOverlay(this);
}

void OverlayTabWidget::setEditHide(bool enable)
{
    if(actEditHide.isChecked() == enable)
        return;
    if(hGrp)
        hGrp->SetBool("EditHide", enable);
    actEditHide.setChecked(enable);
    if(enable) {
        setAutoHide(false);
        setEditShow(false);
    }
    DockWindowManager::instance()->refreshOverlay(this);
}

void OverlayTabWidget::setEditShow(bool enable)
{
    if(actEditShow.isChecked() == enable)
        return;
    if(hGrp)
        hGrp->SetBool("EditShow", enable);
    actEditShow.setChecked(enable);
    if(enable) {
        setAutoHide(false);
        setEditHide(false);
    }
    DockWindowManager::instance()->refreshOverlay(this);
}

QDockWidget *OverlayTabWidget::currentDockWidget() const
{
    int index = -1;
    for(int size : splitter->sizes()) {
        ++index;
        if(size>0)
            return dockWidget(index);
    }
    return dockWidget(currentIndex());
}

QDockWidget *OverlayTabWidget::dockWidget(int index) const
{
    if(index < 0 || index >= splitter->count())
        return nullptr;
    return qobject_cast<QDockWidget*>(splitter->widget(index));
}

void OverlayTabWidget::setOverlayMode(bool enable)
{
    overlayed = enable;

    if(!isVisible() || !count())
        return;

    touched = false;

    titleBar->setVisible(!enable);

    if(!enable && isTransparent())
    {
        setStyleSheet(OverlayStyleSheet::instance()->activeStyleSheet);
        setOverlayMode(this, -1);
    } else {
        if(enable)
            setStyleSheet(OverlayStyleSheet::instance()->onStyleSheet);
        else
            setStyleSheet(OverlayStyleSheet::instance()->offStyleSheet);

        setOverlayMode(this, enable?1:0);
    }

    if(count() <= 1)
        tabBar()->hide();
    else
        tabBar()->setVisible(!enable || !OverlayStyleSheet::instance()->hideTab);

    setRect(rectOverlay);
}

const QRect &OverlayTabWidget::getRect()
{
    return rectOverlay;
}

bool OverlayTabWidget::getAutoHideRect(QRect &rect) const
{
    if(!overlayed || !checkAutoHide())
        return false;
    rect = rectOverlay;
    switch(dockArea) {
    case Qt::RightDockWidgetArea:
        rect.setLeft(rect.left() + std::max(rect.width()-8,0));
        break;
    case Qt::LeftDockWidgetArea:
        rect.setRight(rect.right() - std::max(rect.width()-8,0));
        break;
    case Qt::TopDockWidgetArea:
        rect.setBottom(rect.bottom() - std::max(rect.height()-8,0));
        break;
    case Qt::BottomDockWidgetArea:
        rect.setLeft(rect.left() + autoHideOffset);
        rect.setTop(rect.top() + std::max(rect.height()-8,0));
        break;
    default:
        break;
    }
    return true;
}

void OverlayTabWidget::setOffset(const QSize &ofs)
{
    if(offset != ofs) {
        offset = ofs;
        if(hGrp) {
            hGrp->SetInt("Offset1", ofs.width());
            hGrp->SetInt("Offset3", ofs.height());
        }
    }
}

void OverlayTabWidget::setSizeDelta(int delta)
{
    if(sizeDelta != delta) {
        if(hGrp)
            hGrp->SetInt("Offset2", delta);
        sizeDelta = delta;
    }
}

void OverlayTabWidget::setAutoHideOffset(int offset)
{
    autoHideOffset = offset;
}

void OverlayTabWidget::setRect(QRect rect)
{
    if(rect.width()<=0 || rect.height()<=0)
        return;

    if(hGrp && rect.size() != rectOverlay.size()) {
        hGrp->SetInt("Width", rect.width());
        hGrp->SetInt("Height", rect.height());
    }
    rectOverlay = rect;

    if(getAutoHideRect(rect)) {
        proxyWidget->setGeometry(rect);
        proxyWidget->show();
        hide();
    } else {
        setGeometry(rectOverlay);

        for(int i=0, count=splitter->count(); i<count; ++i)
            splitter->widget(i)->show();

        if(!isVisible() && count()) {
            proxyWidget->hide();
            show();
            setOverlayMode(overlayed);
        }
    }
}

void OverlayTabWidget::addWidget(QDockWidget *dock, const QString &title)
{
    QRect rect = dock->geometry();

    getMainWindow()->removeDockWidget(dock);

    auto titleWidget = dock->titleBarWidget();
    if(titleWidget && titleWidget->objectName()==QLatin1String("OverlayTitle")) {
        auto w = new QWidget();
        w->setObjectName(QLatin1String("OverlayTitle"));
        dock->setTitleBarWidget(w);
        w->hide();
        delete titleWidget;
    }

    dock->show();
    splitter->addWidget(dock);
    addTab(new QWidget(this), title);

    dock->setFeatures(dock->features() & ~QDockWidget::DockWidgetFloatable);
    if(count() == 1)
        setRect(rect);
}

int OverlayTabWidget::dockWidgetIndex(QDockWidget *dock) const
{
    return splitter->indexOf(dock);
}

void OverlayTabWidget::removeWidget(QDockWidget *dock)
{
    int index = dockWidgetIndex(dock);
    if(index < 0)
        return;

    dock->setParent(nullptr);

    auto w = this->widget(index);
    removeTab(index);
    delete w;

    if(!count())
        hide();

    w = dock->titleBarWidget();
    if(w && w->objectName() == QLatin1String("OverlayTitle")) {
        dock->setTitleBarWidget(nullptr);
        delete w;
    }
    DockWindowManager::instance()->setupTitleBar(dock);

    dock->setFeatures(dock->features() | QDockWidget::DockWidgetFloatable);

    setOverlayMode(dock, 0);
}

void OverlayTabWidget::resizeEvent(QResizeEvent *ev)
{
    QTabWidget::resizeEvent(ev);
    timer.start(10);
}

void OverlayTabWidget::setupLayout()
{
    if(count() == 1)
        tabSize = 0;
    else {
        int tsize;
        if(dockArea==Qt::LeftDockWidgetArea || dockArea==Qt::RightDockWidgetArea)
            tsize = tabBar()->width();
        else
            tsize = tabBar()->height();
        if(tsize > tabSize)
            tabSize = tsize;
    }

    QRect rect, rectTitle;
    switch(tabPosition()) {
    case West:
        rectTitle = QRect(tabSize, 0, this->width()-tabSize, titleBar->height());
        rect = QRect(rectTitle.left(), rectTitle.bottom(),
                     rectTitle.width(), this->height()-rectTitle.height());
        break;
    case East:
        rectTitle = QRect(0, 0, this->width()-tabSize, titleBar->height());
        rect = QRect(rectTitle.left(), rectTitle.bottom(),
                     rectTitle.width(), this->height()-rectTitle.height());
        break;
    case North:
        rectTitle = QRect(0, tabSize, titleBar->width(), this->height()-tabSize);
        rect = QRect(rectTitle.right(), rectTitle.top(),
                     this->width()-rectTitle.width(), rectTitle.height());
        break;
    case South:
        rectTitle = QRect(0, 0, titleBar->width(), this->height()-tabSize);
        rect = QRect(rectTitle.right(), rectTitle.top(),
                     this->width()-rectTitle.width(), rectTitle.height());
        break;
    }
    splitter->setGeometry(rect);
    titleBar->setGeometry(rectTitle);
}

void OverlayTabWidget::setCurrent(QDockWidget *widget)
{
    int index = dockWidgetIndex(widget);
    if(index >= 0) 
        setCurrentIndex(index);
}

void OverlayTabWidget::onSplitterMoved()
{
    int index = -1;
    for(int size : splitter->sizes()) {
        ++index;
        if(size) {
            if (index != currentIndex()) {
                QSignalBlocker guard(this);
                setCurrentIndex(index);
            }
            break;
        }
    }
    saveTabs();
}

void OverlayTabWidget::onCurrentChanged(int index)
{
    auto sizes = splitter->sizes();
    int i=0;
    int size = splitter->orientation()==Qt::Vertical ? 
                    height()-tabBar()->height() : width()-tabBar()->width();
    for(auto &s : sizes) {
        if(i++ == index)
            s = size;
        else
            s = 0;
    }
    splitter->setSizes(sizes);
    saveTabs();
}

void OverlayTabWidget::changeSize(int changes, bool checkModify)
{
    auto modifier = checkModify ? QApplication::queryKeyboardModifiers() : Qt::NoModifier;
    if(modifier== Qt::ShiftModifier) {
        offset.rwidth() = std::max(offset.rwidth()+changes, 0);
        return;
    } else if (modifier == (Qt::ShiftModifier | Qt::AltModifier)) {
        offset.rheight() = std::max(offset.rheight()+changes, 0);
        return;
    } else if (modifier == Qt::ControlModifier || modifier == Qt::AltModifier) {
        sizeDelta -= changes;
        return;
    }

    QRect rect = rectOverlay;
    switch(dockArea) {
    case Qt::LeftDockWidgetArea:
        rect.setRight(rect.right() + changes);
        break;
    case Qt::RightDockWidgetArea:
        rect.setLeft(rect.left() - changes);
        break;
    case Qt::TopDockWidgetArea:
        rect.setBottom(rect.bottom() + changes);
        break;
    case Qt::BottomDockWidgetArea:
        rect.setTop(rect.top() - changes);
        break;
    default:
        break;
    }
    setRect(rect);
}

// -----------------------------------------------------------

struct OverlayInfo {
    const char *name;
    OverlayTabWidget *tabWidget;
    Qt::DockWidgetArea dockArea;
    QMap<QDockWidget*, OverlayInfo*> &overlayMap;
    ParameterGrp::handle hGrp;

    OverlayInfo(QWidget *parent, const char *name, Qt::DockWidgetArea pos, QMap<QDockWidget*, OverlayInfo*> &map)
        : name(name), dockArea(pos), overlayMap(map)
    {
        tabWidget = new OverlayTabWidget(parent, dockArea);
        tabWidget->setObjectName(QString::fromLatin1(name));
        tabWidget->getProxyWidget()->setObjectName(tabWidget->objectName() + QString::fromLatin1("Proxy"));
        tabWidget->setMovable(true);
        hGrp = App::GetApplication().GetUserParameter().GetGroup("BaseApp")
                            ->GetGroup("MainWindow")->GetGroup("DockWindows")->GetGroup(name);
    }

    bool addWidget(QDockWidget *dock, bool forced=true) {
        if(!dock)
            return false;
        if(tabWidget->dockWidgetIndex(dock) >= 0)
            return false;
        overlayMap[dock] = this;
        bool visible = dock->isVisible();

        auto focus = qApp->focusWidget();
        if(focus && findTabWidget(focus) != tabWidget)
            focus = nullptr;

        tabWidget->addWidget(dock, dock->windowTitle());

        if(focus) {
            tabWidget->setCurrent(dock);
            focus = qApp->focusWidget();
            if(focus)
                focus->clearFocus();
        }

        if(forced) {
            auto mw = getMainWindow();
            for(auto d : mw->findChildren<QDockWidget*>()) {
                if(mw->dockWidgetArea(d) == dockArea)
                    addWidget(d, false);
            }
            if(visible) {
                dock->show();
                tabWidget->setCurrent(dock);
            }
        } else
            tabWidget->saveTabs();
        return true;
    }

    void removeWidget() {
        if(!tabWidget->count())
            return;

        tabWidget->hide();

        QPointer<QWidget> focus = qApp->focusWidget();

        MainWindow *mw = getMainWindow();
        QDockWidget *lastDock = tabWidget->currentDockWidget();
        if(lastDock) {
            tabWidget->removeWidget(lastDock);
            lastDock->show();
            mw->addDockWidget(dockArea, lastDock);
        }
        while(tabWidget->count()) {
            QDockWidget *dock = tabWidget->dockWidget(0);
            if(!dock) {
                tabWidget->removeTab(0);
                continue;
            }
            tabWidget->removeWidget(dock);
            dock->show();
            if(lastDock)
                mw->tabifyDockWidget(lastDock, dock);
            else
                mw->addDockWidget(dockArea, dock);
            lastDock = dock;
        }

        if(focus)
            focus->setFocus();

        tabWidget->saveTabs();
    }

    bool geometry(QRect &rect) {
        if(!tabWidget->count())
            return false;
        rect = tabWidget->getRect();
        return true;
    }

    void setGeometry(int x, int y, int w, int h)
    {
        if(!tabWidget->count())
            return;
        tabWidget->setRect(QRect(x,y,w,h));
    }

    void save()
    {
    }

    void restore()
    {
        tabWidget->restore(hGrp);
        for(int i=0,c=tabWidget->count();i<c;++i) {
            auto dock = tabWidget->dockWidget(i);
            if(dock)
                overlayMap[dock] = this;
        }
    }

};

#endif // FC_HAS_DOCK_OVERLAY

enum OverlayToggleMode {
    OverlayUnset,
    OverlaySet,
    OverlayToggle,
    OverlayToggleAutoHide,
    OverlayToggleTransparent,
    OverlayCheck,
};

namespace Gui {
struct DockWindowManagerP
{
    QList<QDockWidget*> _dockedWindows;
    QMap<QString, QPointer<QWidget> > _dockWindows;
    DockWindowItems _dockWindowItems;
    QTimer _timer;

#ifdef FC_HAS_DOCK_OVERLAY
    QMap<QDockWidget*, OverlayInfo*> _overlays;
    OverlayInfo _left;
    OverlayInfo _right;
    OverlayInfo _top;
    OverlayInfo _bottom;
    std::array<OverlayInfo*,4> _overlayInfos;

    QAction _actClose;
    QAction _actFloat;
    QAction _actOverlay;
    std::array<QAction*, 3> _actions;

    bool updateStyle = false;

    DockWindowManagerP(DockWindowManager *host, QWidget *parent)
        :_left(parent,"OverlayLeft", Qt::LeftDockWidgetArea,_overlays)
        ,_right(parent,"OverlayRight", Qt::RightDockWidgetArea,_overlays)
        ,_top(parent,"OverlayTop", Qt::TopDockWidgetArea,_overlays)
        ,_bottom(parent,"OverlayBottom",Qt::BottomDockWidgetArea,_overlays)
        ,_overlayInfos({&_left,&_right,&_top,&_bottom})
        ,_actions({&_actOverlay,&_actFloat,&_actClose})
    {
        Application::Instance->signalActivateView.connect([this](const MDIView *) {
            refreshOverlay();
        });
        Application::Instance->signalInEdit.connect([this](const ViewProviderDocumentObject &) {
            refreshOverlay();
        });
        Application::Instance->signalResetEdit.connect([this](const ViewProviderDocumentObject &) {
            refreshOverlay();
        });

        _actOverlay.setIcon(QPixmap(_PixmapOverlay));
        _actOverlay.setData(QString::fromLatin1("OBTN Overlay"));

        const char * const pixmapFloat[]={
            "10 10 2 1",
            ". c None",
            TITLE_BUTTON_COLOR,
            "...#######",
            "...#.....#",
            "...#.....#",
            "#######..#",
            "#.....#..#",
            "#.....#..#",
            "#.....####",
            "#.....#...",
            "#.....#...",
            "#######...",
        };
        _actFloat.setIcon(QPixmap(pixmapFloat));
        _actFloat.setData(QString::fromLatin1("OBTN Float"));

        const char * const pixmapClose[]={
            "10 10 2 1",
            ". c None",
            TITLE_BUTTON_COLOR,
            "##......##",
            "###....###",
            ".###..###.",
            "..######..",
            "...####...",
            "...####...",
            "..######..",
            ".###..###.",
            "###....###",
            "##......##",
        };
        _actClose.setIcon(QPixmap(pixmapClose));
        _actClose.setData(QString::fromLatin1("OBTN Close"));

        retranslate();

        for(auto action : _actions) {
            QObject::connect(action, SIGNAL(triggered(bool)), host, SLOT(onAction()));
        }
        for(auto o : _overlayInfos) {
            for(auto action : o->tabWidget->actions()) {
                QObject::connect(action, SIGNAL(triggered(bool)), host, SLOT(onAction()));
            }
            o->tabWidget->setTitleBar(createTitleBar(o->tabWidget));
        }
    }

    bool toggleOverlay(QDockWidget *dock, OverlayToggleMode toggle,
            Qt::DockWidgetArea dockPos=Qt::NoDockWidgetArea)
    {
        if(!dock)
            return false;

        auto it = _overlays.find(dock);
        if(it != _overlays.end()) {
            auto o = it.value();
            switch(toggle) {
            case OverlayToggleAutoHide:
                o->tabWidget->setAutoHide(!o->tabWidget->isAutoHide());
                break;
            case OverlayToggleTransparent:
                o->tabWidget->setTransparent(!o->tabWidget->isTransparent());
                break;
            case OverlayUnset:
            case OverlayToggle:
                _overlays.erase(it);
                o->removeWidget();
                return false;
            default:
                break;
            }
            return true;
        }

        if(toggle == OverlayUnset)
            return false;

        if(dockPos == Qt::NoDockWidgetArea)
            dockPos = getMainWindow()->dockWidgetArea(dock);
        OverlayInfo *o;
        switch(dockPos) {
        case Qt::LeftDockWidgetArea:
            o = &_left;
            break;
        case Qt::RightDockWidgetArea:
            o = &_right;
            break;
        case Qt::TopDockWidgetArea:
            o = &_top;
            break;
        case Qt::BottomDockWidgetArea:
            o = &_bottom;
            break;
        default:
            return false;
        }
        if(toggle == OverlayCheck && !o->tabWidget->count())
            return false;
        if(o->addWidget(dock)) {
            if(toggle == OverlayToggleAutoHide)
                o->tabWidget->setAutoHide(true);
            else if(toggle == OverlayToggleTransparent)
                o->tabWidget->setTransparent(true);
        }
        return true;
    }

    void refreshOverlay(QWidget *widget=nullptr, bool refreshStyle=false)
    {
        if(refreshStyle) {
            OverlayStyleSheet::instance()->update();
            updateStyle = true;
        }

        if(widget) {
            auto tabWidget = findTabWidget(widget);
            if(tabWidget && tabWidget->count()) {
                for(auto o : _overlayInfos) {
                    if(tabWidget == o->tabWidget) {
                        tabWidget->touch();
                        break;
                    }
                }
            }
        }
        _timer.start(widget?1:ViewParams::getDockOverlayDelay());
    }

    void saveOverlay()
    {
        _left.save();
        _right.save();
        _top.save();
        _bottom.save();
    }

    void restoreOverlay()
    {
        _left.restore();
        _right.restore();
        _top.restore();
        _bottom.restore();
        refreshOverlay();
    }

    void onTimer()
    {
        QMdiArea *mdi = getMainWindow()->findChild<QMdiArea*>();
        if(!mdi)
            return;

        auto focus = findTabWidget(qApp->focusWidget());
        auto active = findTabWidget(qApp->widgetAt(QCursor::pos()));
        OverlayTabWidget *reveal = nullptr;

        bool updateFocus = false;
        bool updateActive = false;

        for(auto o : _overlayInfos) {
            if(o->tabWidget->isTouched() || updateStyle) {
                if(o->tabWidget == focus)
                    updateFocus = true;
                else if(o->tabWidget == active)
                    updateActive = true;
                else 
                    o->tabWidget->setOverlayMode(true);
            }
            if(!o->tabWidget->getRevealTime().isNull()) {
                if(o->tabWidget->getRevealTime()<= QTime::currentTime())
                    o->tabWidget->setRevealTime(QTime());
                else
                    reveal = o->tabWidget;
            }
        }
        updateStyle = false;

        if(focus && (focus->isOverlayed() || updateFocus)) {
            focus->setOverlayMode(false);
            focus->raise();
            if(reveal == focus)
                reveal = nullptr;
        }

        if(active) {
            if(active != focus && (active->isOverlayed() || updateActive)) 
                active->setOverlayMode(false);
            active->raise();
            if(reveal == active)
                reveal = nullptr;
        }

        if(reveal) {
            reveal->setOverlayMode(false);
            reveal->raise();
        }

        for(auto o : _overlayInfos) {
            if(o->tabWidget != focus 
                    && o->tabWidget != active
                    && o->tabWidget != reveal
                    && o->tabWidget->count()
                    && !o->tabWidget->isOverlayed())
            {
                o->tabWidget->setOverlayMode(true);
            }
        }

        int w = mdi->geometry().width();
        int h = mdi->geometry().height();
        auto tabbar = mdi->findChild<QTabBar*>();
        if(tabbar)
            h -= tabbar->height();

        QRect rectBottom(0,0,0,0);
        if(_bottom.geometry(rectBottom)) {
            QSize ofs = _bottom.tabWidget->getOffset();
            int delta = _bottom.tabWidget->getSizeDelta();
            h -= ofs.height();
            int bw = std::max(w-ViewParams::getNaviWidgetSize()-10-ofs.width()-delta, 10);
            // Bottom width is maintain the same to reduce QTextEdit re-layout
            // which may be expensive if there are lots of text, e.g. for
            // ReportView or PythonConsole.
            _bottom.setGeometry(ofs.width(),h-rectBottom.height(),bw,rectBottom.height());
            _bottom.tabWidget->getAutoHideRect(rectBottom);
        }
        QRect rectLeft(0,0,0,0);
        if(_left.geometry(rectLeft)) {
            auto ofs = _left.tabWidget->getOffset();
            int delta = _left.tabWidget->getSizeDelta();
            int lh = std::max(h-rectBottom.height()-ofs.width()-delta,10);
            _left.setGeometry(ofs.height(),ofs.width(),rectLeft.width(),lh);
            _left.tabWidget->getAutoHideRect(rectLeft);

            _bottom.tabWidget->setAutoHideOffset(rectLeft.width()-ofs.width());
        } else 
            _bottom.tabWidget->setAutoHideOffset(0);

        QRect rectRight(0,0,0,0);
        if(_right.geometry(rectRight)) {
            auto ofs = _right.tabWidget->getOffset();
            int delta = _right.tabWidget->getSizeDelta();
            int dh = std::max(rectBottom.height(), ViewParams::getNaviWidgetSize()-10);
            int rh = std::max(h-dh-ofs.width()-delta, 10);
            w -= ofs.height();
            _right.setGeometry(w-rectRight.width(),ofs.width(),rectRight.width(),rh);
            _right.tabWidget->getAutoHideRect(rectRight);
        }
        QRect rectTop(0,0,0,0);
        if(_top.geometry(rectTop)) {
            auto ofs = _top.tabWidget->getOffset();
            int delta = _top.tabWidget->getSizeDelta();
            int tw = std::max(w-rectLeft.width()-rectRight.width()-ofs.width()-delta,10);
            _top.setGeometry(rectLeft.width()-ofs.width(),ofs.height(),tw,rectTop.height());
        }
    }

    void setOverlayMode(DockWindowManager::OverlayMode mode)
    {
        switch(mode) {
        case DockWindowManager::DisableAll:
        case DockWindowManager::EnableAll: {
            auto docks = getMainWindow()->findChildren<QDockWidget*>();
            // put visible dock widget first
            std::sort(docks.begin(),docks.end(),
                [](const QDockWidget *a, const QDockWidget *) {
                    return !a->visibleRegion().isEmpty();
                });
            for(auto dock : docks) {
                if(mode == DockWindowManager::DisableAll)
                    toggleOverlay(dock, OverlayUnset);
                else
                    toggleOverlay(dock, OverlaySet);
            }
            return;
        }
        case DockWindowManager::ToggleAll:
            for(auto o : _overlayInfos) {
                if(o->tabWidget->count()) {
                    setOverlayMode(DockWindowManager::DisableAll);
                    return;
                }
            }
            setOverlayMode(DockWindowManager::EnableAll);
            return;
        case DockWindowManager::AutoHideAll: {
            bool found = false;
            for(auto o : _overlayInfos) {
                if(o->tabWidget->count())
                    found = true;
            }
            if(!found)
                setOverlayMode(DockWindowManager::EnableAll);
        }
        // fall through
        case DockWindowManager::AutoHideNone:
            for(auto o : _overlayInfos)
                o->tabWidget->setAutoHide(mode == DockWindowManager::AutoHideAll);
            refreshOverlay();
            return;
        case DockWindowManager::ToggleAutoHideAll:
            for(auto o : _overlayInfos) {
                if(o->tabWidget->count() && o->tabWidget->isAutoHide()) {
                    setOverlayMode(DockWindowManager::AutoHideNone);
                    return;
                }
            }
            setOverlayMode(DockWindowManager::AutoHideAll);
            return;
        case DockWindowManager::TransparentAll: {
            bool found = false;
            for(auto o : _overlayInfos) {
                if(o->tabWidget->count())
                    found = true;
            }
            if(!found)
                setOverlayMode(DockWindowManager::EnableAll);
        }
        // fall through
        case DockWindowManager::TransparentNone:
            for(auto o : _overlayInfos)
                o->tabWidget->setTransparent(mode == DockWindowManager::TransparentAll);
            refreshOverlay();
            return;
        case DockWindowManager::ToggleTransparentAll:
            for(auto o : _overlayInfos) {
                if(o->tabWidget->count() && o->tabWidget->isTransparent()) {
                    setOverlayMode(DockWindowManager::TransparentNone);
                    return;
                }
            }
            setOverlayMode(DockWindowManager::TransparentAll);
            return;
        default:
            break;
        }

        OverlayToggleMode m;
        QDockWidget *dock = nullptr;
        for(auto w=qApp->widgetAt(QCursor::pos()); w; w=w->parentWidget()) {
            dock = qobject_cast<QDockWidget*>(w);
            if(dock)
                break;
            auto tabWidget = qobject_cast<OverlayTabWidget*>(w);
            if(tabWidget) {
                dock = tabWidget->currentDockWidget();
                if(dock)
                    break;
            }
        }
        if(!dock) {
            for(auto w=qApp->focusWidget(); w; w=w->parentWidget()) {
                dock = qobject_cast<QDockWidget*>(w);
                if(dock)
                    break;
            }
        }

        switch(mode) {
        case DockWindowManager::ToggleActive:
            m = OverlayToggle;
            break;
        case DockWindowManager::ToggleAutoHide:
            m = OverlayToggleAutoHide;
            break;
        case DockWindowManager::ToggleTransparent:
            m = OverlayToggleTransparent;
            break;
        case DockWindowManager::EnableActive:
            m = OverlaySet;
            break;
        case DockWindowManager::DisableActive:
            m = OverlayUnset;
            break;
        default:
            return;
        }
        toggleOverlay(dock, m);
    }

    void onToggleDockWidget(QDockWidget *dock, bool checked)
    {
        if(!dock)
            return;

        auto it = _overlays.find(dock);
        if(it == _overlays.end()) {
            if(!checked)
                return;
            toggleOverlay(dock, OverlayCheck);
            it = _overlays.find(dock);
            if(it == _overlays.end())
                return;
        }
        if(checked) {
            int index = it.value()->tabWidget->dockWidgetIndex(dock);
            if(index >= 0) {
                auto sizes = it.value()->tabWidget->getSplitter()->sizes();
                if(index >= sizes.size() || sizes[index]==0) 
                    it.value()->tabWidget->setCurrent(dock);
                else {
                    QSignalBlocker guard(it.value()->tabWidget);
                    it.value()->tabWidget->setCurrent(dock);
                }
            }
            it.value()->tabWidget->setRevealTime(QTime::currentTime().addMSecs(
                    ViewParams::getDockOverlayRevealDelay()));
            refreshOverlay();
        } else {
            it.value()->tabWidget->removeWidget(dock);
            getMainWindow()->addDockWidget(it.value()->dockArea, dock);
            _overlays.erase(it);
        }
    }

    void changeOverlaySize(int changes)
    {
        auto tabWidget = findTabWidget(qApp->widgetAt(QCursor::pos()));
        if(tabWidget) {
            tabWidget->changeSize(changes, false);
            refreshOverlay();
        }
    }

    void onFocusChanged(QWidget *, QWidget *) {
        refreshOverlay();
    }

    void setupTitleBar(QDockWidget *dock)
    {
        if(!dock->titleBarWidget())
            dock->setTitleBarWidget(createTitleBar(nullptr));
    }

    QWidget *createTitleBar(QWidget *parent)
    {
        auto widget = new QWidget(parent);
        widget->setObjectName(QLatin1String("OverlayTitle"));

        bool vertical = false;
        QBoxLayout *layout = nullptr;
        auto tabWidget = qobject_cast<OverlayTabWidget*>(parent);
        if(!tabWidget) {
           layout = new QBoxLayout(QBoxLayout::LeftToRight, widget); 
        } else {
            switch(tabWidget->getDockArea()) {
            case Qt::LeftDockWidgetArea:
                layout = new QBoxLayout(QBoxLayout::LeftToRight, widget); 
                break;
            case Qt::RightDockWidgetArea:
                layout = new QBoxLayout(QBoxLayout::RightToLeft, widget); 
                break;
            case Qt::TopDockWidgetArea:
                layout = new QBoxLayout(QBoxLayout::TopToBottom, widget); 
                vertical = true;
                break;
            case Qt::BottomDockWidgetArea:
                layout = new QBoxLayout(QBoxLayout::BottomToTop, widget); 
                vertical = true;
                break;
            default:
                break;
            }
        }
        layout->addSpacing(5);
        layout->setContentsMargins(1,1,1,1);
        if(parent) {
            for(auto action : parent->actions())
                layout->addWidget(createTitleButton(action));
        } else {
            for(auto action : _actions)
                layout->addWidget(createTitleButton(action));
        }
        // layout->addStretch(2);
        layout->addSpacerItem(new QSpacerItem(_TitleButtonSize,_TitleButtonSize,
                    vertical?QSizePolicy::Minimum:QSizePolicy::Expanding,
                    vertical?QSizePolicy::Expanding:QSizePolicy::Minimum));
        return widget;
    }

    QWidget *createTitleButton(QAction *action)
    {
        auto button = new OverlayToolButton(nullptr);
        button->setObjectName(action->data().toString());
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setContentsMargins(0,0,0,0);
        button->setFixedSize(_TitleButtonSize,_TitleButtonSize);
        return button;
    }

    void onAction(QAction *action) {
        if(action == &_actOverlay) {
            DockWindowManager::instance()->setOverlayMode(DockWindowManager::ToggleActive);
        } else if(action == &_actFloat || action == &_actClose) {
            for(auto w=qApp->widgetAt(QCursor::pos());w;w=w->parentWidget()) {
                auto dock = qobject_cast<QDockWidget*>(w);
                if(!dock)
                    continue;
                if(action == &_actClose) {
                    dock->toggleViewAction()->activate(QAction::Trigger);
                } else {
                    auto it = _overlays.find(dock);
                    if(it != _overlays.end()) {
                        it.value()->tabWidget->removeWidget(dock);
                        getMainWindow()->addDockWidget(it.value()->dockArea, dock);
                        _overlays.erase(it);
                        dock->show();
                        dock->setFloating(true);
                        refreshOverlay();
                    } else 
                        dock->setFloating(!dock->isFloating());
                }
                return;
            }
        } else {
            auto tabWidget = qobject_cast<OverlayTabWidget*>(action->parent());
            if(tabWidget)
                tabWidget->onAction(action);
        }
    }

    void retranslate()
    {
        _actOverlay.setToolTip(QObject::tr("Toggle overlay"));
        _actFloat.setToolTip(QObject::tr("Toggle floating window"));
        _actClose.setToolTip(QObject::tr("Close dock window"));
    }

#else // FC_HAS_DOCK_OVERLAY

    DockWindowManagerP(DockWindowManager *, QWidget *) {}
    void refreshOverlay(QWidget *, bool) {}
    void saveOverlay() {}
    void restoreOverlay() {}
    void onTimer() {}
    void setOverlayMode(DockWindowManager::OverlayMode) {}
    void onToggleDockWidget(QDockWidget *, bool) {}
    void changeOverlaySize(int) {}
    void onFocusChanged(QWidget *, QWidget *) {}
    void onAction(QAction *) {}
    void setupTitleBar(QDockWidget *) {}
    void retranslate() {}

    bool toggleOverlay(QDockWidget *, OverlayToggleMode,
            Qt::DockWidgetArea dockPos=Qt::NoDockWidgetArea)
    {
        (void)dockPos;
        return false;
    }

#endif // FC_HAS_DOCK_OVERLAY
};
} // namespace Gui

DockWindowManager* DockWindowManager::_instance = 0;

DockWindowManager* DockWindowManager::instance()
{
    if ( _instance == 0 )
        _instance = new DockWindowManager;
    return _instance;
}

void DockWindowManager::destruct()
{
    delete _instance;
    _instance = 0;
}

DockWindowManager::DockWindowManager()
{
    auto mdi = getMainWindow()->findChild<QMdiArea*>();
    assert(mdi);
    mdi->installEventFilter(this);
    d = new DockWindowManagerP(this,mdi);
    connect(&d->_timer, SIGNAL(timeout()), this, SLOT(onTimer()));
    d->_timer.setSingleShot(true);

    connect(qApp, SIGNAL(focusChanged(QWidget*,QWidget*)),
            this, SLOT(onFocusChanged(QWidget*,QWidget*)));
}

DockWindowManager::~DockWindowManager()
{
    d->_dockedWindows.clear();
    delete d;
}

void DockWindowManager::setOverlayMode(OverlayMode mode)
{
    d->setOverlayMode(mode);
}

/**
 * Adds a new dock window to the main window and embeds the given \a widget.
 */
QDockWidget* DockWindowManager::addDockWindow(const char* name, QWidget* widget, Qt::DockWidgetArea pos)
{
    if(!widget)
        return nullptr;
    QDockWidget *dw = qobject_cast<QDockWidget*>(widget->parentWidget());
    if(dw)
        return dw;

    // creates the dock widget as container to embed this widget
    MainWindow* mw = getMainWindow();
    dw = new QDockWidget(mw);
    d->setupTitleBar(dw);

    // Note: By default all dock widgets are hidden but the user can show them manually in the view menu.
    // First, hide immediately the dock widget to avoid flickering, after setting up the dock widgets
    // MainWindow::loadLayoutSettings() is called to restore the layout.
    dw->hide();
    switch (pos) {
    case Qt::LeftDockWidgetArea:
    case Qt::RightDockWidgetArea:
    case Qt::TopDockWidgetArea:
    case Qt::BottomDockWidgetArea:
        mw->addDockWidget(pos, dw);
    default:
        break;
    }
    connect(dw, SIGNAL(destroyed(QObject*)),
            this, SLOT(onDockWidgetDestroyed(QObject*)));
    connect(widget, SIGNAL(destroyed(QObject*)),
            this, SLOT(onWidgetDestroyed(QObject*)));

    // add the widget to the dock widget
    widget->setParent(dw);
    dw->setWidget(widget);

    // set object name and window title needed for i18n stuff
    dw->setObjectName(QLatin1String(name));
    dw->setWindowTitle(QDockWidget::trUtf8(name));
    dw->setFeatures(QDockWidget::AllDockWidgetFeatures);

    d->_dockedWindows.push_back(dw);

    connect(dw->toggleViewAction(), SIGNAL(triggered(bool)), this, SLOT(onToggleDockWidget(bool)));
    return dw;
}

void DockWindowManager::onToggleDockWidget(bool checked)
{
    auto action = qobject_cast<QAction*>(sender());
    if(!action)
        return;
    d->onToggleDockWidget(qobject_cast<QDockWidget*>(action->parent()), checked);
}

/**
 * Returns the widget inside the dock window by name.
 * If it does not exist 0 is returned.
 */
QWidget* DockWindowManager::getDockWindow(const char* name) const
{
    for (QList<QDockWidget*>::ConstIterator it = d->_dockedWindows.begin(); it != d->_dockedWindows.end(); ++it) {
        if ((*it)->objectName() == QLatin1String(name))
            return (*it)->widget();
    }

    return 0;
}

/**
 * Returns a list of all widgets inside the dock windows.
 */
QList<QWidget*> DockWindowManager::getDockWindows() const
{
    QList<QWidget*> docked;
    for (QList<QDockWidget*>::ConstIterator it = d->_dockedWindows.begin(); it != d->_dockedWindows.end(); ++it)
        docked.push_back((*it)->widget());
    return docked;
}

/**
 * Removes the specified dock window with name \name without deleting it.
 */
QWidget* DockWindowManager::removeDockWindow(const char* name)
{
    QWidget* widget=0;
    for (QList<QDockWidget*>::Iterator it = d->_dockedWindows.begin(); it != d->_dockedWindows.end(); ++it) {
        if ((*it)->objectName() == QLatin1String(name)) {
            QDockWidget* dw = *it;
            d->_dockedWindows.erase(it);
            d->toggleOverlay(dw, OverlayUnset);
            getMainWindow()->removeDockWidget(dw);
            // avoid to destruct the embedded widget
            widget = dw->widget();
            widget->setParent(0);
            dw->setWidget(0);
            disconnect(dw, SIGNAL(destroyed(QObject*)),
                       this, SLOT(onDockWidgetDestroyed(QObject*)));
            disconnect(widget, SIGNAL(destroyed(QObject*)),
                       this, SLOT(onWidgetDestroyed(QObject*)));
            delete dw; // destruct the QDockWidget, i.e. the parent of the widget
            break;
        }
    }

    return widget;
}

/**
 * Method provided for convenience. Does basically the same as the method above unless that
 * it accepts a pointer.
 */
void DockWindowManager::removeDockWindow(QWidget* widget)
{
    if (!widget)
        return;
    for (QList<QDockWidget*>::Iterator it = d->_dockedWindows.begin(); it != d->_dockedWindows.end(); ++it) {
        if ((*it)->widget() == widget) {
            QDockWidget* dw = *it;
            d->_dockedWindows.erase(it);
            d->toggleOverlay(dw, OverlayUnset);
            getMainWindow()->removeDockWidget(dw);
            // avoid to destruct the embedded widget
            widget->setParent(0);
            dw->setWidget(0);
            disconnect(dw, SIGNAL(destroyed(QObject*)),
                       this, SLOT(onDockWidgetDestroyed(QObject*)));
            disconnect(widget, SIGNAL(destroyed(QObject*)),
                       this, SLOT(onWidgetDestroyed(QObject*)));
            delete dw; // destruct the QDockWidget, i.e. the parent of the widget
            break;
        }
    }
}

/**
 * Sets the window title for the dockable windows.
 */
void DockWindowManager::retranslate()
{
    for (QList<QDockWidget*>::Iterator it = d->_dockedWindows.begin(); it != d->_dockedWindows.end(); ++it) {
        (*it)->setWindowTitle(QDockWidget::tr((*it)->objectName().toLatin1()));
    }
    d->retranslate();
}

/**
 * Appends a new \a widget with \a name to the list of available dock widgets. The caller must make sure that
 * the name is unique. If a widget with this name is already registered nothing is done but false is returned,
 * otherwise it is appended and true is returned.
 *
 * As default the following widgets are already registered:
 * \li Std_TreeView
 * \li Std_PropertyView
 * \li Std_ReportView
 * \li Std_ToolBox
 * \li Std_ComboView
 * \li Std_SelectionView
 *
 * To avoid name clashes the caller should use names of the form \a module_widgettype, i. e. if a analyse dialog for
 * the mesh module is added the name must then be Mesh_AnalyzeDialog. 
 *
 * To make use of dock windows when a workbench gets loaded the method setupDockWindows() must reimplemented in a 
 * subclass of Gui::Workbench. 
 */
bool DockWindowManager::registerDockWindow(const char* name, QWidget* widget)
{
    QMap<QString, QPointer<QWidget> >::Iterator it = d->_dockWindows.find(QLatin1String(name));
    if (it != d->_dockWindows.end() || !widget)
        return false;
    d->_dockWindows[QLatin1String(name)] = widget;
    widget->hide(); // hide the widget if not used
    return true;
}

QWidget* DockWindowManager::unregisterDockWindow(const char* name)
{
    QWidget* widget = 0;
    QMap<QString, QPointer<QWidget> >::Iterator it = d->_dockWindows.find(QLatin1String(name));
    if (it != d->_dockWindows.end()) {
        widget = d->_dockWindows.take(QLatin1String(name));
    }

    return widget;
}

QWidget* DockWindowManager::findRegisteredDockWindow(const char* name)
{
    QMap<QString, QPointer<QWidget> >::Iterator it = d->_dockWindows.find(QLatin1String(name));
    if (it != d->_dockWindows.end())
        return it.value();
    return nullptr;
}

/** Sets up the dock windows of the activated workbench. */
void DockWindowManager::setup(DockWindowItems* items)
{
    // save state of current dock windows
    saveState();
    d->_dockWindowItems = *items;

    ParameterGrp::handle hPref = App::GetApplication().GetUserParameter().GetGroup("BaseApp")
                               ->GetGroup("MainWindow")->GetGroup("DockWindows");
    QList<QDockWidget*> docked = d->_dockedWindows;
    const QList<DockWindowItem>& dws = items->dockWidgets();

    for (QList<DockWindowItem>::ConstIterator it = dws.begin(); it != dws.end(); ++it) {
        QDockWidget* dw = findDockWidget(docked, it->name);
        QByteArray dockName = it->name.toLatin1();
        bool visible = hPref->GetBool(dockName.constData(), it->visibility);

        if (!dw) {
            QMap<QString, QPointer<QWidget> >::ConstIterator jt = d->_dockWindows.find(it->name);
            if (jt != d->_dockWindows.end()) {
                dw = addDockWindow(jt.value()->objectName().toUtf8(), jt.value(), it->pos);
                jt.value()->show();
                dw->toggleViewAction()->setData(it->name);
                dw->setVisible(visible);
                if(!visible)
                    continue;
            }
        }
        else {
            dw->setVisible(visible);
            dw->toggleViewAction()->setVisible(true);
            int index = docked.indexOf(dw);
            docked.removeAt(index);
        }

        if(dw)
            d->toggleOverlay(dw, OverlayCheck);
    }
}

void DockWindowManager::saveState()
{
    ParameterGrp::handle hPref = App::GetApplication().GetUserParameter().GetGroup("BaseApp")
                               ->GetGroup("MainWindow")->GetGroup("DockWindows");

    const QList<DockWindowItem>& dockItems = d->_dockWindowItems.dockWidgets();
    for (QList<DockWindowItem>::ConstIterator it = dockItems.begin(); it != dockItems.end(); ++it) {
        QDockWidget* dw = findDockWidget(d->_dockedWindows, it->name);
        if (dw) {
            QByteArray dockName = dw->toggleViewAction()->data().toByteArray();
            hPref->SetBool(dockName.constData(), dw->isVisible());
        }
    }
}

QDockWidget* DockWindowManager::findDockWidget(const QList<QDockWidget*>& dw, const QString& name) const
{
    for (QList<QDockWidget*>::ConstIterator it = dw.begin(); it != dw.end(); ++it) {
        if ((*it)->toggleViewAction()->data().toString() == name)
            return *it;
    }

    return 0;
}

void DockWindowManager::onDockWidgetDestroyed(QObject* dw)
{
    for (QList<QDockWidget*>::Iterator it = d->_dockedWindows.begin(); it != d->_dockedWindows.end(); ++it) {
        if (*it == dw) {
            d->_dockedWindows.erase(it);
            break;
        }
    }
}

void DockWindowManager::onWidgetDestroyed(QObject* widget)
{
    for (QList<QDockWidget*>::Iterator it = d->_dockedWindows.begin(); it != d->_dockedWindows.end(); ++it) {
        // make sure that the dock widget is not about to being deleted
        if ((*it)->metaObject() != &QDockWidget::staticMetaObject) {
            disconnect(*it, SIGNAL(destroyed(QObject*)),
                       this, SLOT(onDockWidgetDestroyed(QObject*)));
            d->_dockedWindows.erase(it);
            break;
        }

        if ((*it)->widget() == widget) {
            // Delete the widget if not used anymore
            QDockWidget* dw = *it;
            dw->deleteLater();
            break;
        }
    }
}

void DockWindowManager::onTimer()
{
    d->onTimer();
}

bool DockWindowManager::eventFilter(QObject *o, QEvent *ev)
{
    switch(ev->type()) {
    case QEvent::Resize:
        if(qobject_cast<QMdiArea*>(o))
            refreshOverlay();
        return false;
    default:
        break;
    }
    return false;
}

void DockWindowManager::refreshOverlay(QWidget *widget, bool refreshStyle)
{
    d->refreshOverlay(widget, refreshStyle);
}

void DockWindowManager::saveOverlay()
{
    d->saveOverlay();
}

void DockWindowManager::restoreOverlay()
{
    d->restoreOverlay();
}

void DockWindowManager::changeOverlaySize(int changes)
{
    d->changeOverlaySize(changes);
}

void DockWindowManager::onFocusChanged(QWidget *old, QWidget *now)
{
    d->onFocusChanged(old, now);
}

void DockWindowManager::setupTitleBar(QDockWidget *dock)
{
    d->setupTitleBar(dock);
}

void DockWindowManager::onAction()
{
    QAction *action = qobject_cast<QAction*>(sender());
    if(action)
        d->onAction(action);
}

#include "moc_DockWindowManager.cpp"

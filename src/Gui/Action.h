/***************************************************************************
 *   Copyright (c) 2004 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#ifndef GUI_ACTION_H
#define GUI_ACTION_H

#include <memory>
#include <deque>
#include <map>

#include <QAction>
#include <QMenu>
#include <QComboBox>
#include <QKeySequence>
#include <QCompleter>
#include <QTimer>

#include <boost_signals2.hpp>

#include <Base/Parameter.h>

class QLineEdit;
class QWidgetAction;
class QCheckBox;
class QToolBar;
class QSpinBox;

namespace Gui
{
class Command;

/**
 * The Action class is the link between Qt's QAction class and FreeCAD's
 * command classes (@ref Command). So, it is possible to have all actions
 * (from toolbars, menus, ...) implemented in classes instead of many slot
 * methods in the class @ref MainWindow.
 * @author Werner Mayer
 */
class GuiExport Action : public QObject
{
    Q_OBJECT

public:
    Action (Command* pcCmd, QObject * parent = 0);
    /// Action takes ownership of the 'action' object.
    Action (Command* pcCmd, QAction* action, QObject * parent);
    virtual ~Action();

    virtual void addTo (QWidget * w);
    virtual void setEnabled(bool);
    virtual void setVisible(bool);

    void setCheckable(bool);
    void setChecked (bool, bool no_signal=false);
    bool isChecked() const;
    bool isEnabled() const;

    void setShortcut (const QString &);
    QKeySequence shortcut() const;
    void setIcon (const QIcon &);
    QIcon icon() const;
    void setStatusTip (const QString &);
    QString statusTip() const;
    void setText (const QString &);
    QString text() const;
    void setToolTip (const QString &, const QString &title = QString());
    QString toolTip() const;
    void setWhatsThis (const QString &);
    QString whatsThis() const;
    void setMenuRole(QAction::MenuRole menuRole);
    QAction *action() const {
        return _action;
    }

    static QAction *addCheckBox(QMenu *menu,
                                const QString &txt,
                                const QString &tooltip = QString(),
                                const QIcon &icon = QIcon(),
                                bool checked = false,
                                QCheckBox **checkbox = nullptr);

    static QAction *addCheckBox(QMenu *menu,
                                const QString &txt,
                                bool checked,
                                QCheckBox **checkbox = nullptr,
                                const QString &tooltip = QString(),
                                const QIcon &icon = QIcon());

    static QAction *addWidget(QMenu *menu,
                              const QString &txt,
                              const QString &tooltip,
                              QWidget *widget,
                              bool needLable = true);

    static QString createToolTip(QString tooltip,
                                 const QString &title,
                                 const QFont &font,
                                 const QString &shortcut,
                                 const Command *cmd = nullptr,
                                 QString iconPath = QString());

    /** Obtain tool tip of a given command
     * @param cmd: input command
     * @param richFormat: whether to output rich text formatted tooltip
     */
    static QString commandToolTip(const Command *cmd, bool richFormat = true);

    /** Obtain the menu text of a given command
     * @param cmd: input command
     * @return Return the command menu text that is stripped with its mnemonic
     * symbol '&' and ending punctuations
     */
    static QString commandMenuText(const Command *cmd);

    /// Clean the title by stripping the mnemonic symbol '&' and ending punctuations
    static QString cleanTitle(const QString &title);

    Command *command() const {
        return _pcCmd;
    }

public Q_SLOTS:
    virtual void onActivated ();
    virtual void onToggled   (bool);

protected:
    QAction* _action;
    Command *_pcCmd;
    QString _tooltip;
    QString _title;
};

// --------------------------------------------------------------------

/**
 * The ActionGroup class is the link between Qt's QActionGroup class and
 * FreeCAD's command classes (@ref Command). Compared to Action with an
 * ActionGroup it is possible to implement a single command with a group
 * of toggable actions where e.g. one is set exclusive.
 * @author Werner Mayer
 */
class GuiExport ActionGroup : public Action
{
    Q_OBJECT

public:
    ActionGroup (Command* pcCmd, QObject * parent = 0);
    virtual ~ActionGroup();

    void addTo (QWidget * w);
    void setEnabled (bool);
    void setDisabled (bool);
    void setExclusive (bool);
    bool isExclusive() const;
    void setVisible (bool);
    void setIsMode(bool b) { _isMode = b; }

    void setDropDownMenu(bool b) { _dropDown = b; }
    QAction* addAction(QAction*);
    QAction* addAction(const QString&);
    QList<QAction*> actions() const;
    int checkedAction() const;
    void setCheckedAction(int);

public Q_SLOTS:
    void onActivated ();
    void onToggled(bool);
    void onActivated (QAction*);

protected:
    QActionGroup* _group;
    bool _dropDown;
    bool _external;
    bool _toggle;
    bool _isMode;
    QList<QAction*> _actions;
};

// --------------------------------------------------------------------

class WorkbenchGroup;
class GuiExport WorkbenchComboBox : public QComboBox
{
    Q_OBJECT

public:
    WorkbenchComboBox(WorkbenchGroup* wb, QWidget* parent=0);
    virtual ~WorkbenchComboBox();
    void showPopup();
    void setAction(QAction *act) {action = act;}
    QAction *getAction() {return action;}

public Q_SLOTS:
    void onActivated(int);
    void onActivated(QAction*);
    void populate();

protected Q_SLOTS:
    void onWorkbenchActivated(const QString&);

private:
    WorkbenchGroup* group;
    QAction *action = nullptr;
};

// --------------------------------------------------------------------

class GuiExport WorkbenchTabWidget : public QTabWidget
{
    Q_OBJECT
public:
    WorkbenchTabWidget(WorkbenchGroup* wb, QWidget* parent=0);
    virtual ~WorkbenchTabWidget();
    void setAction(QAction *act) {action = act;}
    QAction *getAction() {return action;}
    void setupVisibility();

protected:
    bool eventFilter(QObject *, QEvent *ev);

protected Q_SLOTS:
    void onCurrentChanged();
    void onTabMoved(int from, int to);
    void onWorkbenchActivated(const QString&);
    void updateWorkbenches();
    void onChangeOrientation();
    QToolBar *getToolBar();

    friend class WorkbenchGroup;

private:
    WorkbenchGroup* group;
    boost::signals2::scoped_connection connParam;
    QTimer timer;
    QTimer timerCurrentChange;
    bool moved = false;
    QAction *action = nullptr;
};

class WorkbenchTabBar: public QTabBar
{
    Q_PROPERTY(int tabSize READ tabSize WRITE setTabSize)
    Q_OBJECT
public:
    WorkbenchTabBar(QWidget *parent)
        :QTabBar(parent)
    {}

    int tabSize() const {return _tabSize;}
    void setTabSize(int size);

protected:
    QSize tabSizeHint(int index) const;
    void changeEvent(QEvent*);

private:
    int _tabSize = 0;
    bool hasTabSize = false;
};
/**
 * The WorkbenchGroup class represents a list of workbenches. When it is added
 * to a menu a submenu gets created, if added to a toolbar a combo box gets created.
 * @author Werner Mayer
 */
class GuiExport WorkbenchGroup : public ActionGroup
{
    Q_OBJECT

public:
    /**
     * Creates an action for the command \a pcCmd to load the workbench \a name
     * when it gets activated.
     */
    WorkbenchGroup (Command* pcCmd, QObject * parent);
    virtual ~WorkbenchGroup();
    void addTo (QWidget * w);
    void refreshWorkbenchList();

    void slotActivateWorkbench(const char*);
    void slotAddWorkbench(const char*);
    void slotRemoveWorkbench(const char*);

protected:
    void customEvent(QEvent* e);

protected Q_SLOTS:
    void onShowMenu();
    void onContextMenuRequested(const QPoint &);

Q_SIGNALS:
    void workbenchListUpdated();

private:
    void setWorkbenchData(int i, const QString& wb);

    friend class WorkbenchTabWidget;
    friend class WorkbenchComboBox;

private:
    QMenu* _menu = nullptr;

    class Private;
    friend class Private;
    std::unique_ptr<Private> _pimpl;
};

// --------------------------------------------------------------------

/**
 * The RecentFilesAction class holds a menu listed with the recent files.
 * @author Werner Mayer
 */
class GuiExport RecentFilesAction : public ActionGroup
{
    Q_OBJECT

public:
    RecentFilesAction (Command* pcCmd, QObject * parent = 0);
    virtual ~RecentFilesAction();

    void appendFile(const QString&);
    void activateFile(int);
    void resizeList(int);
    QStringList files() const;

private:
    void setFiles(const QStringList&);
    void restore();
    void save();

private:
    int visibleItems; /**< Number of visible items */
    int maximumItems; /**< Number of maximum items */

    class Private;
    friend class Private;
    std::unique_ptr<Private> _pimpl;
};

// --------------------------------------------------------------------

/**
 * The RecentMacrosAction class holds a menu listed with the recent macros
 * that were executed, edited, or created
 */
class GuiExport RecentMacrosAction : public ActionGroup
{
    Q_OBJECT

public:
    RecentMacrosAction (Command* pcCmd, QObject * parent = 0);
    virtual ~RecentMacrosAction();

    void appendFile(const QString&);
    void activateFile(int);
    void resizeList(int);

private:
    void setFiles(const QStringList&);
    QStringList files() const;
    void restore();
    void save();

private:
    int visibleItems; /**< Number of visible items */
    int maximumItems; /**< Number of maximum items */
    std::string shortcut_modifiers; /**< default = "Ctrl+Shift+" */
    int shortcut_count; /**< Number of dynamic shortcuts to create -- default = 3*/
};


// --------------------------------------------------------------------

/**
 * The UndoAction class reimplements a special behaviour to make a menu
 * appearing when the button with the arrow is clicked.
 * @author Werner Mayer
 */
class GuiExport UndoAction : public Action
{
    Q_OBJECT

public:
    UndoAction (Command* pcCmd,QObject * parent = 0);
    ~UndoAction();
    void addTo (QWidget * w);
    void setEnabled(bool);
    void setVisible(bool);

private Q_SLOTS:
    void actionChanged();

private:
    QAction* _toolAction;
};

// --------------------------------------------------------------------

/**
 * The RedoAction class reimplements a special behaviour to make a menu
 * appearing when the button with the arrow is clicked.
 * @author Werner Mayer
 */
class GuiExport RedoAction : public Action
{
    Q_OBJECT

public:
    RedoAction (Command* pcCmd,QObject * parent = 0);
    ~RedoAction();
    void addTo ( QWidget * w );
    void setEnabled(bool);
    void setVisible(bool);

private Q_SLOTS:
    void actionChanged();

private:
    QAction* _toolAction;
};

// --------------------------------------------------------------------

/**
 * Special action to show all dockable views -- except of toolbars -- in an own popup menu.
 * @author Werner Mayer
 */
class GuiExport DockWidgetAction : public Action
{
    Q_OBJECT

public:
    DockWidgetAction (Command* pcCmd, QObject * parent = 0);
    virtual ~DockWidgetAction();
    void addTo (QWidget * w);

private:
    QMenu* _menu;
};

// --------------------------------------------------------------------

/**
 * Special action to show all toolbars in an own popup menu.
 * @author Werner Mayer
 */
class GuiExport ToolBarAction : public Action
{
    Q_OBJECT

public:
    ToolBarAction (Command* pcCmd, QObject * parent = 0);
    virtual ~ToolBarAction();
    void addTo (QWidget * w);

private:
    QMenu* _menu;
};

// --------------------------------------------------------------------

/**
 * @author Werner Mayer
 */
class GuiExport WindowAction : public ActionGroup
{
    Q_OBJECT

public:
    WindowAction (Command* pcCmd, QObject * parent = 0);
    virtual ~WindowAction();
    void addTo (QWidget * w);

private:
    QMenu* _menu;
};

// --------------------------------------------------------------------

/**
 * Special action for view camera binding.
 */
class GuiExport ViewCameraBindingAction : public Action
{
    Q_OBJECT

public:
    ViewCameraBindingAction (Command* pcCmd, QObject * parent = 0);
    virtual ~ViewCameraBindingAction();
    void addTo (QWidget * w);

protected Q_SLOTS:
    void onShowMenu();
    void onTriggered(QAction *);

private:
    QMenu* _menu;
};

/**
 * Special action for Std_SelUp command.
 */
class GuiExport SelUpAction : public Action
{
    Q_OBJECT

public:
    SelUpAction (Command* pcCmd, QObject * parent = 0);
    virtual ~SelUpAction();
    void addTo (QWidget * w);
    void popup(const QPoint &pt);

protected Q_SLOTS:
    void onShowMenu();

private:
    QMenu* _menu;
    QAction *_emptyAction;
};

/**
 * Special action for Std_SelBack and Std_SelForward command.
 */
class GuiExport SelStackAction : public Action
{
    Q_OBJECT

public:
    enum class Type {
        Backward,
        Forward
    };
    SelStackAction (Command* pcCmd, Type type, QObject * parent = 0);
    virtual ~SelStackAction();

    void addTo (QWidget * w);

protected:
    void populate();
    void select(int idx, const std::vector<int> &subIndices={});

private:
    QMenu* _menu;
    Type _type;
};

/**
 * Special action for Std_CmdHistory command.
 */
class GuiExport CmdHistoryAction : public Action
{
    Q_OBJECT

public:
    CmdHistoryAction (Command* pcCmd, QObject * parent = 0);
    virtual ~CmdHistoryAction();
    void addTo (QWidget * w);
    void popup(const QPoint &pt);
    static void onInvokeCommand(const char *, bool force=false);
    static std::vector<Command*> recentCommands();

protected:
    bool eventFilter(QObject *, QEvent *ev);

protected Q_SLOTS:
    void onShowMenu();
    void onCommandActivated(const QByteArray &);
    void onNewAction();

private:
    QMenu *_menu = nullptr;
    QWidgetAction *_widgetAction = nullptr;
    QLineEdit *_lineedit = nullptr;
    QCompleter *_completer = nullptr;
    QAction *_newAction = nullptr;
};

/**
 * Special action for Std_CmdToolbarMenus command.
 */
class GuiExport ToolbarMenuAction : public Action
{
    Q_OBJECT

public:
    ToolbarMenuAction (Command* pcCmd, QObject * parent = 0);
    virtual ~ToolbarMenuAction();
    void addTo (QWidget * w);
    void popup(const QPoint &pt);

    static void populate();
    static std::string paramName(const char *name, const char *workbench=nullptr);

protected Q_SLOTS:
    virtual void onShowMenu();

protected:
    virtual void update();

protected:
    QMenu* _menu;
    class Private;
    friend class Private;
    std::unique_ptr<Private> _pimpl;
};

/**
 * Special action for Std_Expression command.
 */
class GuiExport ExpressionAction : public Action
{
    Q_OBJECT

public:
    ExpressionAction (Command* pcCmd, QObject * parent = 0);
    virtual ~ExpressionAction();
    void addTo (QWidget * w);
    void popup(const QPoint &pt);

protected Q_SLOTS:
    void onShowMenu();
    void onAction(QAction *action);

protected:
    QMenu* _menu;
    class Private;
    friend class Private;
    std::unique_ptr<Private> _pimpl;
};

/**
 * Special action for Std_CmdPresets command.
 */
class GuiExport PresetsAction : public Action
{
    Q_OBJECT

public:
    PresetsAction (Command* pcCmd, QObject * parent = 0);
    virtual ~PresetsAction();
    void addTo(QWidget * w);
    void popup(const QPoint &pt);
    void push(const QString &name);
    static PresetsAction *instance();

protected Q_SLOTS:
    void onShowMenu();
    void onAction(QAction *action);

private:
    QMenu* _menu;
    QMenu* _undoMenu;
    std::deque<std::pair<QString, ParameterGrp::handle>> _undos;
};

GuiExport void setupMenuStyle(QWidget *);

} // namespace Gui

#endif // GUI_ACTION_H

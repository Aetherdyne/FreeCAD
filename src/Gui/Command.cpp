/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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
#include "Widgets.h"
#ifndef _PreComp_
# include <sstream>
# include <QApplication>
# include <QByteArray>
# include <QDir>
# include <QKeySequence>
# include <QMessageBox>
# include <Inventor/actions/SoGetBoundingBoxAction.h>
# include <Inventor/nodes/SoOrthographicCamera.h>
# include <Inventor/nodes/SoPerspectiveCamera.h>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <Python.h>
#include <frameobject.h>

#include "Command.h"
#include "Action.h"
#include "Application.h"
#include "Document.h"
#include "Selection.h"
#include "Macro.h"
#include "MainWindow.h"
#include "DlgUndoRedo.h"
#include "BitmapFactory.h"
#include "WhatsThis.h"
#include "WaitCursor.h"
#include "Control.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "WorkbenchManager.h"
#include "Workbench.h"
#include "ShortcutManager.h"

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Base/Sequencer.h>
#include <Base/Tools.h>

#include <App/Document.h>
#include <App/DocumentParams.h>
#include <App/DocumentObject.h>
#include <App/AutoTransaction.h>
#include <Gui/ViewProviderLink.h>
#include "PieMenu.h"

FC_LOG_LEVEL_INIT("Command", true, true)

using Base::Interpreter;
using namespace Gui;
using namespace Gui::Dialog;
using namespace Gui::DockWnd;

/** \defgroup commands Command Framework
    \ingroup GUI
    \brief Structure for registering commands to the FreeCAD system
 * \section Overview
 * In GUI applications many commands can be invoked via a menu item, a toolbar button or an accelerator key. The answer of Qt to master this
 * challenge is the class \a QAction. A QAction object can be added to a popup menu or a toolbar and keep the state of the menu item and
 * the toolbar button synchronized.
 *
 * For example, if the user clicks the menu item of a toggle action then the toolbar button gets also pressed
 * and vice versa. For more details refer to your Qt documentation.
 *
 * \section Drawbacks
 * Since QAction inherits QObject and emits the \a triggered() signal or \a toggled() signal for toggle actions it is very convenient to connect
 * these signals e.g. with slots of your MainWindow class. But this means that for every action an appropriate slot of MainWindow is necessary
 * and leads to an inflated MainWindow class. Furthermore, it's simply impossible to provide plugins that may also need special slots -- without
 * changing the MainWindow class.
 *
 * \section wayout Way out
 * To solve these problems we have introduced the command framework to decouple QAction and MainWindow. The base classes of the framework are
 * \a Gui::CommandBase and \a Gui::Action that represent the link between Qt's QAction world and the FreeCAD's command world.
 *
 * The Action class holds a pointer to QAction and CommandBase and acts as a mediator and -- to save memory -- that gets created
 * (@ref Gui::CommandBase::createAction()) not before it is added (@ref Gui::Command::addTo()) to a menu or toolbar.
 *
 * Now, the implementation of the slots of MainWindow can be done in the method \a activated() of subclasses of Command instead.
 *
 * For example, the implementation of the "Open file" command can be done as follows.
 * \code
 * class OpenCommand : public Command
 * {
 * public:
 *   OpenCommand() : Command("Std_Open")
 *   {
 *     // set up menu text, status tip, ...
 *     sMenuText     = "&Open";
 *     sToolTipText  = "Open a file";
 *     sWhatsThis    = "Open a file";
 *     sStatusTip    = "Open a file";
 *     sPixmap       = "Open"; // name of a registered pixmap
 *     sAccel        = "Shift+P"; // or "P" or "P, L" or "Ctrl+X, Ctrl+C" for a sequence
 *   }
 * protected:
 *   void activated(int)
 *   {
 *     QString filter ... // make a filter of all supported file formats
 *     QStringList FileList = QFileDialog::getOpenFileNames( filter,QString(), getMainWindow() );
 *     for ( QStringList::Iterator it = FileList.begin(); it != FileList.end(); ++it ) {
 *       getGuiApplication()->open((*it).latin1());
 *     }
 *   }
 * };
 * \endcode
 * An instance of \a OpenCommand must be created and added to the \ref Gui::CommandManager to make the class known to FreeCAD.
 * To see how menus and toolbars can be built go to the @ref workbench.
 *
 * @see Gui::Command, Gui::CommandManager
 */

// list of modules already loaded by a command (not issue again for macro cleanness)
std::set<std::string> alreadyLoadedModule;

class StringCache {
public:
    static const char* New(const char* str) {
        using StringList = std::list<std::string>;
        static StringList strings;
        strings.emplace_back(str);
        return strings.back().c_str();
    }
};

CommandBase::CommandBase(const char* sMenu, const char* sToolTip, const char* sWhat,
                         const char* sStatus, const char* sPixmap, const char* sAcc)
  : sMenuText(sMenu)
  , sToolTipText(sToolTip)
  , sWhatsThis(sWhat ? sWhat : sToolTip)
  , sStatusTip(sStatus ? sStatus : sToolTip)
  , sPixmap(sPixmap)
  , sAccel(sAcc)
  , _pcAction(nullptr)
{
}

CommandBase::~CommandBase()
{
    //Note: The Action object becomes a children of MainWindow which gets destroyed _before_ the
    //command manager hence before any command object. So the action pointer is a dangling pointer
    //at this state.

    // Command can be destroyed before the the MainWindow, for example, dynamic
    // command created (and later deleted) by user for a pie menu.
    if (getMainWindow())
        delete _pcAction;
}

Action* CommandBase::getAction() const
{
    return _pcAction;
}

Action * CommandBase::createAction()
{
    // does nothing
    return 0;
}

void CommandBase::setMenuText(const char* s)
{
    this->sMenuText = StringCache::New(s);
}

void CommandBase::setToolTipText(const char* s)
{
    this->sToolTipText = StringCache::New(s);
}

void CommandBase::setStatusTip(const char* s)
{
    this->sStatusTip = StringCache::New(s);
}

void CommandBase::setWhatsThis(const char* s)
{
    this->sWhatsThis = StringCache::New(s);
}

void CommandBase::setPixmap(const char* s)
{
    this->sPixmap = StringCache::New(s);
}

void CommandBase::setAccel(const char* s)
{
    this->sAccel = StringCache::New(s);
}

//===========================================================================
// Command
//===========================================================================

/* TRANSLATOR Gui::Command */

Command::Command(const char* name)
    : CommandBase(nullptr)
    , sName(name)
    , sHelpUrl(0)
{
    sAppModule  = "FreeCAD";
    sGroup      = "Standard";
    eType       = AlterDoc | Alter3DView | AlterSelection;
    bEnabled    = true;
    bCanLog     = true;
}

Command::~Command()
{
}

void Command::setShortcut(const QString &shortcut)
{
    if (_pcAction)
        _pcAction->setShortcut(shortcut);
}

QString Command::getShortcut() const
{
    if (_pcAction)
        return _pcAction->shortcut().toString();
    return ShortcutManager::instance()->getShortcut(getName());
}

bool Command::isViewOfType(Base::Type t) const
{
    Gui::Document *d = getGuiApplication()->activeDocument();
    if (!d) return false;
    Gui::BaseView *v = d->getActiveView();
    if (!v) return false;
    if (v->getTypeId().isDerivedFrom(t))
        return true;
    else
        return false;
}

void Command::initAction()
{
    if (!_pcAction) {
        BitmapCacheContext ctx(getName());
        _pcAction = createAction();
#ifdef FC_DEBUG
        // Accelerator conflict can now be dynamically resolved in ShortcutManager
        //
        // printConflictingAccelerators();
#endif
        setShortcut(ShortcutManager::instance()->getShortcut(getName(), getAccel()));
        testActive();
    }
}

void Command::addTo(QWidget *pcWidget)
{
    initAction();
    _pcAction->addTo(pcWidget);
}

void Command::addToGroup(ActionGroup* group, bool checkable)
{
    addToGroup(group);
    _pcAction->setCheckable(checkable);
}

void Command::addToGroup(ActionGroup* group)
{
    initAction();
    group->addAction(_pcAction->action());
}

Application *Command::getGuiApplication(void)
{
    return Application::Instance;
}

Gui::Document* Command::getActiveGuiDocument(void) const
{
    return getGuiApplication()->activeDocument();
}

App::Document* Command::getDocument(const char* Name) const
{
    if (Name) {
        return App::GetApplication().getDocument(Name);
    }
    else {
        Gui::Document * pcDoc = getGuiApplication()->activeDocument();
        if (pcDoc)
            return pcDoc->getDocument();
        else
            return 0l;
    }
}

App::DocumentObject* Command::getObject(const char* Name) const
{
    App::Document*pDoc = getDocument();
    if (pDoc)
        return pDoc->getObject(Name);
    else
        return 0;
}

int Command::_busy;

class PendingLine {
public:
    PendingLine(MacroManager::LineType type, const char *line) {
        Application::Instance->macroManager()->addLine(type,line,true);
    }
    ~PendingLine() {
        cancel();
    }
    void cancel() {
        Application::Instance->macroManager()->addLine(MacroManager::Cmt,0,true);
    }
};

class CommandTrigger {
public:
    CommandTrigger(Command::TriggerSource &trigger, Command::TriggerSource source)
        :trigger(trigger),saved(trigger)
    {
        trigger = source;
    }

    ~CommandTrigger() {
        trigger = saved;
    }
private:
    Command::TriggerSource &trigger;
    Command::TriggerSource saved;
};

void Command::setupCheckable(int iMsg) {
    QAction *action = 0;
    Gui::ActionGroup* pcActionGroup = qobject_cast<Gui::ActionGroup*>(_pcAction);
    if(pcActionGroup) {
        QList<QAction*> a = pcActionGroup->actions();
        assert(iMsg < a.size());
        action = a[iMsg];
    }else
        action = _pcAction->action();

    if(!action)
        return;

    bool checkable = action->isCheckable();
    _pcAction->setCheckable(checkable);
    if(checkable) {
        bool checked = false;
        switch(triggerSource()) {
        case TriggerNone:
            checked = !action->isChecked();
            break;
        case TriggerAction:
            checked = _pcAction->isChecked();
            break;
        case TriggerChildAction:
            checked = action->isChecked();
            break;
        }
        bool blocked = action->blockSignals(true);
        action->setChecked(checked);
        action->blockSignals(blocked);
        if(action!=_pcAction->action())
            _pcAction->setChecked(checked,true);
    }

}

namespace {

QAction *_getChildQAction(Action *pcAction, int idx = -1, int *pindex=nullptr)
{
    if (!pcAction)
        return nullptr;
    auto actionGroup = qobject_cast<ActionGroup*>(pcAction);
    if(actionGroup) {
        if (idx < 0)
            idx = pcAction->property("defaultAction").toInt();
        auto actions = actionGroup->actions();
        if (idx >= 0 && idx < actions.size()) {
            if (pindex)
                *pindex = idx;
            return actions[idx];
        }
    }
    return nullptr;
}

Action *_getChildAction(Action *pcAction, int idx = -1, int *pindex=nullptr)
{
    if (auto act = _getChildQAction(pcAction, idx, pindex))
        return qobject_cast<Action*>(act->parent());
    return nullptr;
}

} // anonymous namespace

Action* Command::getChildAction(int idx, int *pindex) const
{
    return _getChildAction(_pcAction, idx, pindex);
}

void Command::onInvoke(int index) {
    if(index>=0 && triggerSource()==TriggerChildAction) {
        if (auto qaction = _getChildQAction(_pcAction, index)) {
            if (auto action = qobject_cast<Action*>(qaction->parent())) {
                if (auto cmd = action->command()) {
                    if(cmd->getType() & NoDefaultAction)
                        return;
                }
            }
            _pcAction->setProperty("defaultAction", QVariant(index));
            setup(_pcAction);
        }
    }
}

void Command::invoke(int i, TriggerSource trigger)
{
    if (trigger != TriggerNone)
        CmdHistoryAction::onInvokeCommand(getName());

    CommandTrigger cmdTrigger(_trigger,trigger);

    onInvoke(i);

    if (!Application::Instance->commandManager().onInvokeCommand(getName(), i))
        return;

    if (displayText.empty()) {
        displayText = getMenuText();
        boost::replace_all(displayText,"&","");
        if (displayText.empty())
            displayText = getName();
    }

    // Do not query _pcAction since it isn't created necessarily
#ifdef FC_LOGUSERACTION
    Base::Console().Log("CmdG: %s\n",sName);
#endif

    _invoke(i, bCanLog && !_busy);
}

void Command::_invoke(int id, bool disablelog)
{
    try {
        App::AutoTransaction committer(
                (App::DocumentParams::getViewObjectTransaction()
                || (eType&NoTransaction)) ? "" : displayText.c_str(), true);


        // set the application module type for the macro
        getGuiApplication()->macroManager()->setModule(sAppModule);

        std::unique_ptr<LogDisabler> logdisabler;
        if (disablelog)
            logdisabler.reset(new LogDisabler);

        // check if it really works NOW (could be a delay between click deactivation of the button)
        if (isActive()) {
            auto manager = getGuiApplication()->macroManager();
            auto editDoc = getGuiApplication()->editDocument();

            if (!logdisabler) {
                activated(id);
            }
            else {
                Gui::SelectionLogDisabler seldisabler;
                auto lines = manager->getLines();
                std::ostringstream ss;
                ss << "### Begin command " << sName;
                // Add a pending line to mark the start of a command
                PendingLine pending(MacroManager::Cmt, ss.str().c_str());
                ss.str("");

                activated(id);

                if (manager->getLines() == lines) {
                    // This command does not record any lines, lets do it for
                    // it. The above LogDisabler is to prevent nested command
                    // logging, i.e. we only auto log the first invoking
                    // command.

                    // Cancel the above pending line first
                    pending.cancel();
                    ss << "Gui.runCommand('" << sName << "'," << id << ')';
                    manager->addLine(MacroManager::Gui, ss.str().c_str());
                }
                else {
                    // In case the command has any output to the console, lets
                    // mark the end of the command here
                    ss << "### End command " << sName;
                    manager->addLine(MacroManager::Cmt, ss.str().c_str());
                }
            }
            if (! (eType & NoUpdateActions))
                getMainWindow()->updateActions();

            // If this command starts an editing, let the transaction persist
            if (!editDoc && getGuiApplication()->editDocument())
                committer.setEnable(false);
        }
    }
    catch (const Base::SystemExitException&) {
        throw;
    }
    catch (Base::PyException &e) {
        e.ReportException();
    }
    catch (Py::Exception&) {
        Base::PyGILStateLocker lock;
        Base::PyException e;
        e.ReportException();
    }
    catch (Base::AbortException&) {
    }
    catch (Base::Exception &e) {
        e.ReportException();
        // Pop-up a dialog for FreeCAD-specific exceptions
        QMessageBox::critical(Gui::getMainWindow(),
                              QObject::tr("Exception"),
                              QString::fromUtf8(e.what()));
    }
    catch (std::exception &e) {
        Base::Console().Error("C++ exception thrown (%s)\n", e.what());
    }
    catch (const char* e) {
        Base::Console().Error("%s\n", e);
    }
#ifndef FC_DEBUG
    catch (...) {
        Base::Console().Error("Gui::Command::activated(%d): Unknown C++ exception thrown\n", id);
    }
#endif
}

void Command::testActive(void)
{
    if (!_pcAction)
        return;

    if (_blockCmd || !bEnabled) {
        _pcAction->setEnabled(false);
        return;
    }

    if (!(eType & ForEdit)) { // special case for commands which are only in some edit modes active

        if ((!Gui::Control().isAllowedAlterDocument()  && eType & AlterDoc)    ||
            (!Gui::Control().isAllowedAlterView()      && eType & Alter3DView) ||
            (!Gui::Control().isAllowedAlterSelection() && eType & AlterSelection)) {
             _pcAction->setEnabled(false);
            return;
        }
    }

    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    if(pcAction) {
        Gui::CommandManager &rcCmdMgr = Gui::Application::Instance->commandManager();
        for(auto action : pcAction->actions()) {
            auto name = action->property("CommandName").toByteArray();
            if(!name.size())
                continue;
            Command* cmd = rcCmdMgr.getCommandByName(name);
            if(cmd)
                action->setEnabled(cmd->isActive());
        }
    }

    bool bActive = isActive();
    _pcAction->setEnabled(bActive);
}

void Command::setEnabled(bool on)
{
    if (_pcAction) {
        bEnabled = on;
        _pcAction->setEnabled(on);
    }
}

//--------------------------------------------------------------------------
// Helper methods
//--------------------------------------------------------------------------

bool Command::hasActiveDocument(void) const
{
    return getActiveGuiDocument() != 0;
}
/// true when there is a document and a Feature with Name
bool Command::hasObject(const char* Name)
{
    return getDocument() != 0 && getDocument()->getObject(Name) != 0;
}

Gui::SelectionSingleton&  Command::getSelection(void)
{
    return Gui::Selection();
}

std::string Command::getUniqueObjectName(const char *BaseName, const App::DocumentObject *obj)
{
    auto doc = obj?obj->getDocument():App::GetApplication().getActiveDocument();
    assert(doc);
    return doc->getUniqueObjectName(BaseName);
}

std::string Command::getObjectCmd(const char *Name, const App::Document *doc,
        const char *prefix, const char *postfix, bool gui)
{
    if(!doc) doc = App::GetApplication().getActiveDocument();
    if(!doc || !Name)
        return std::string("None");
    std::ostringstream str;
    if(prefix)
        str << prefix;
    str << (gui?"Gui":"App") << ".getDocument('" << doc->getName()
        << "').getObject('" << Name << "')";
    if(postfix)
        str << postfix;
    return str.str();
}

std::string Command::getObjectCmd(const App::DocumentObject *obj,
        const char *prefix, const char *postfix, bool gui)
{
    if(!obj || !obj->getNameInDocument())
        return std::string("None");
    return getObjectCmd(obj->getNameInDocument(), obj->getDocument(), prefix, postfix,gui);
}

void Command::setAppModuleName(const char* s)
{
    this->sAppModule = StringCache::New(s);
}

void Command::setGroupName(const char* s)
{
    this->sGroup = StringCache::New(s);
}

QString Command::translatedGroupName() const
{
    QString text = qApp->translate(className(), getGroupName());
    if (text == QString::fromUtf8(getGroupName()))
        text = qApp->translate("CommandGroup", getGroupName());
    return text;
}

//--------------------------------------------------------------------------
// UNDO REDO transaction handling
//--------------------------------------------------------------------------
/** Open a new Undo transaction on the active document
 *  This method opens a new UNDO transaction on the active document. This transaction
 *  will later appear in the UNDO REDO dialog with the name of the command. If the user
 *  recall the transaction everything changed on the document between OpenCommand() and
 *  CommitCommand will be undone (or redone). You can use an alternative name for the
 *  operation default is the Command name.
 *  @see CommitCommand(),AbortCommand()
 */
void Command::openCommand(const char* sCmdName)
{
    if (!sCmdName)
        sCmdName = "Command";
    App::GetApplication().setActiveTransaction(sCmdName);
}

void Command::commitCommand(void)
{
    App::GetApplication().closeActiveTransaction();
}

void Command::abortCommand(void)
{
    App::GetApplication().closeActiveTransaction(true);
}

bool Command::hasPendingCommand(void)
{
    return !!App::GetApplication().getActiveTransaction();
}

bool Command::_blockCmd = false;

void Command::blockCommand(bool block)
{
    Command::_blockCmd = block;
}

/// Run a App level Action
void Command::_doCommand(const char *file, int line, DoCmd_Type eType, const char* sCmd, ...)
{
    va_list ap;
    va_start(ap, sCmd);
    QString s;
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    const QString cmd = s.vsprintf(sCmd, ap);
#else
    const QString cmd = s.vasprintf(sCmd, ap);
#endif
    va_end(ap);

    // 'vsprintf' expects a utf-8 string for '%s'
    QByteArray format = cmd.toUtf8();

#ifdef FC_LOGUSERACTION
    Base::Console().Log("CmdC: %s\n", format.constData());
#endif

    _runCommand(file,line,eType,format.constData());
}

void Command::printPyCaller() {
    if(!FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
        return;
    PyFrameObject* frame = PyEval_GetFrame();
    if(!frame)
        return;
    int line = PyFrame_GetLineNumber(frame);
#if PY_VERSION_HEX < 0x030b0000
    const char *file = PyUnicode_AsUTF8(frame->f_code->co_filename);
    printCaller(file?file:"<no file>",line);
#else
    PyCodeObject* code = PyFrame_GetCode(frame);
    const char* file = PyUnicode_AsUTF8(code->co_filename);
    printCaller(file?file:"<no file>",line);
    Py_DECREF(code);
#endif
}

void Command::printCaller(const char *file, int line) {
    if(!file || !FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
        return;
    std::ostringstream str;
#ifdef FC_OS_WIN32
    const char *_f = std::strstr(file, "\\src\\");
#else
    const char *_f = std::strstr(file, "/src/");
#endif
    str << "## " << (_f?_f+5:file)<<'('<<line<<')';
    Gui::Application::Instance->macroManager()->addLine(MacroManager::Cmt,str.str().c_str());
}

/// Run a App level Action
void Command::_runCommand(const char *file, int line, DoCmd_Type eType, const char* sCmd)
{
    LogDisabler d1;
    SelectionLogDisabler d2;
    Base::PyGILStateLocker lock;

    printCaller(file,line);
    if (eType == Gui)
        Gui::Application::Instance->macroManager()->addLine(MacroManager::Gui,sCmd);
    else
        Gui::Application::Instance->macroManager()->addLine(MacroManager::App,sCmd);

    try {
        Base::Interpreter().runString(sCmd);
    }
    catch(Py::Exception &) {
        Base::PyException::ThrowException();
    }
}

/// Run a App level Action
void Command::_runCommand(const char *file, int line, DoCmd_Type eType, const QByteArray& sCmd)
{
    _runCommand(file,line,eType,sCmd.constData());
}

void Command::addModule(DoCmd_Type eType,const char* sModuleName)
{
    if(alreadyLoadedModule.find(sModuleName) == alreadyLoadedModule.end()) {
        LogDisabler d1;
        SelectionLogDisabler d2;
        std::string sCmd("import ");
        sCmd += sModuleName;
        if (eType == Gui)
            Gui::Application::Instance->macroManager()->addLine(MacroManager::Gui,sCmd.c_str());
        else
            Gui::Application::Instance->macroManager()->addLine(MacroManager::App,sCmd.c_str());
        Base::Interpreter().runString(sCmd.c_str());
        alreadyLoadedModule.insert(sModuleName);
    }
}

std::string Command::_assureWorkbench(const char *file, int line, const char * sName)
{
    // check if the WB is already open?
    std::string actName = WorkbenchManager::instance()->active()->name();
    // if yes, do nothing
    if(actName == sName)
        return actName;

    // else - switch to new WB
    _doCommand(file,line,Gui,"Gui.activateWorkbench('%s')",sName);

    return actName;

}

void Command::_copyVisual(const char *file, int line, const char* to, const char* attr, const char* from)
{
    _copyVisual(file,line,to,attr,from,attr);
}

void Command::_copyVisual(const char *file, int line, const char* to, const char* attr_to, const char* from, const char* attr_from)
{
    auto doc = App::GetApplication().getActiveDocument();
    if(!doc)
        return;
    return _copyVisual(file,line,doc->getObject(to),attr_to,
            doc->getObject(from),attr_from);
}

void Command::_copyVisual(const char *file, int line, const App::DocumentObject *to, const char* attr_to, const App::DocumentObject *from, const char *attr_from)
{
    if(!from || !from->getNameInDocument() || !to || !to->getNameInDocument())
        return;
    static std::map<std::string,std::string> attrMap = {
        {"ShapeColor","ShapeMaterial.DiffuseColor"},
        // {"LineColor","ShapeMaterial.DiffuseColor"},
        // {"PointColor","ShapeMaterial.DiffuseColor"},
        {"Transparency","Transparency"},
    };
    auto it = attrMap.find(attr_to);
    auto objCmd = getObjectCmd(to);
    if(it!=attrMap.end()) {
        auto obj = from;
        for(int depth=0;;++depth) {
            auto vp = dynamic_cast<Gui::ViewProviderLink*>(
                    Gui::Application::Instance->getViewProvider(obj));
            if(vp && vp->OverrideMaterial.getValue()) {
                _doCommand(file,line,Gui,"%s.ViewObject.%s=%s.ViewObject.%s",
                        objCmd.c_str(),attr_to,getObjectCmd(obj).c_str(),it->second.c_str());
                return;
            }
            auto linked = obj->getLinkedObject(false,0,false,depth);
            if(!linked || linked==obj)
                break;
            obj = linked;
        }
    }

    try {
        _doCommand(file,line,Gui,
                "%s.ViewObject.%s=getattr(%s.getLinkedObject(True).ViewObject,'%s',%s.ViewObject.%s)",
                objCmd.c_str(),attr_to,getObjectCmd(from).c_str(),attr_from,objCmd.c_str(),attr_to);
    }
    catch(Base::Exception& /*e*/) {
        // e.ReportException();
    }
}

void Command::_copyVisual(const char *file, int line, const App::DocumentObject *to, const char* attr, const App::DocumentObject *from)
{
    _copyVisual(file,line,to,attr,from,attr);
}

std::string Command::getPythonTuple(const std::string& name, const std::vector<std::string>& subnames)
{
    std::stringstream str;
    std::vector<std::string>::const_iterator last = --subnames.end();
    str << "(App.ActiveDocument." << name << ",[";
    for (std::vector<std::string>::const_iterator it = subnames.begin();it!=subnames.end();++it){
        str << "\"" << *it << "\"";
        if (it != last)
            str << ",";
    }
    str << "])";
    return str.str();
}

const std::string Command::strToPython(const char* Str)
{
    return Base::InterpreterSingleton::strToPython(Str);
}

/// Updates the (active) document (propagate changes)
void Command::updateActive(void)
{
    WaitCursor wc;
    doCommand(App,"App.ActiveDocument.recompute()");
}

bool Command::isActiveObjectValid(void)
{
    Gui::Document* active = Gui::Application::Instance->activeDocument();
    assert(active);
    App::Document* document = active->getDocument();
    App::DocumentObject* object = document->getActiveObject();
    assert(object);
    return object->isValid();
}

/// Updates the (all or listed) documents (propagate changes)
void Command::updateAll(std::list<Gui::Document*> cList)
{
    if (cList.size()>0) {
        for (std::list<Gui::Document*>::iterator It= cList.begin();It!=cList.end();++It)
            (*It)->onUpdate();
    }
    else {
        Gui::Application::Instance->onUpdate();
    }
}

static inline QIcon commandIcon(Command *cmd)
{
    if (!cmd)
        return QIcon();
    BitmapCacheContext ctx(cmd->getName());
    QIcon icon;
    if (auto action = cmd->getAction())
        icon = action->icon();
    if (icon.isNull())
        icon = BitmapFactory().iconFromTheme(cmd->getPixmap());
    if (!icon.isNull() && !cmd->isActive()) {
        auto sizes = icon.availableSizes(QIcon::Disabled);
        QSize size(64, 64);
        if (sizes.size())
            size = sizes[0];
        icon = icon.pixmap(size, QIcon::Disabled);
    }
    return icon;
}

bool Command::isActive() {
    if (triggerSource() == TriggerNone) {
        if (auto qaction = _getChildQAction(_pcAction)) {
            if (auto action = qobject_cast<Action*>(qaction->parent())) {
                if (auto cmd = action->command()) {
                    cmd->isActive();
                    setActionIcon(_pcAction, commandIcon(cmd));
                } else
                    setActionIcon(_pcAction, qaction->icon());
            } else
                setActionIcon(_pcAction, qaction->icon());
            _pcAction->setChecked(qaction->isChecked(),true);
        }
    }
    return true;
}

void Command::activated(int iMsg)
{
    auto action = getChildAction(iMsg);
    if (!action)
        return;
    if (auto cmd = action->command()) {
        if(triggerSource()==TriggerChildAction) {
            if(!(cmd->getType() & NoDefaultAction))
                _pcAction->setChecked(action->isChecked(),true);
        } else
            cmd->invoke(_pcAction->isChecked());
    }
}


//--------------------------------------------------------------------------
// Online help handling
//--------------------------------------------------------------------------

/// returns the begin of a online help page
const char * Command::beginCmdHelp(void)
{
    return  "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 Transitional//EN\">\n"
            "<html>\n"
            "<head>\n"
            "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=ISO-8859-1\">\n"
            "<title>FreeCAD Main Index</title>\n"
            "</head>\n"
            "<body bgcolor=\"#ffffff\">\n\n";
}

/// returns the end of a online help page
const char * Command::endCmdHelp(void)
{
    return "</body></html>\n\n";
}

void Command::applyCommandData(const char* context, Action* action)
{
    action->setText(QCoreApplication::translate(
        context, getMenuText()));
    action->setToolTip(QCoreApplication::translate(
        context, getToolTipText()));
    action->setWhatsThis(QCoreApplication::translate(
        context, getWhatsThis()));
    if (sStatusTip)
        action->setStatusTip(QCoreApplication::translate(
            context, getStatusTip()));
    else
        action->setStatusTip(QCoreApplication::translate(
            context, getToolTipText()));
}

const char* Command::keySequenceToAccel(int sk) const
{
    /* Local class to ensure free()'ing the strings allocated below */
    typedef std::map<int, std::string> StringMap;
    static StringMap strings;
    StringMap::iterator i = strings.find(sk);

    if (i != strings.end())
        return i->second.c_str();

    QKeySequence::StandardKey type = (QKeySequence::StandardKey)sk;
    QKeySequence ks(type);
    QString qs = ks.toString();
    QByteArray data = qs.toUtf8();

    return (strings[sk] = static_cast<const char*>(data)).c_str();
}

void Command::adjustCameraPosition()
{
    Gui::Document* doc = Gui::Application::Instance->editDocument();
    if (!doc)
        return;

    ViewProviderDocumentObject *parent = nullptr;
    std::string subname;
    auto vp = doc->getInEdit(&parent, &subname);
    if (!parent) {
        parent = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(vp);
        if (!parent)
            return;
    }

    Gui::View3DInventor* view = static_cast<Gui::View3DInventor*>(doc->getActiveView());
    if (!view)
        return;
    view->getViewer()->viewObjects({App::SubObjectT(parent->getObject(), subname.c_str())}, true);
}

void Command::printConflictingAccelerators() const
{
    auto cmd = Application::Instance->commandManager().checkAcceleratorForConflicts(sAccel, this);
    if (cmd)
        Base::Console().Warning("Accelerator conflict between %s (%s) and %s (%s)\n", sName, sAccel, cmd->sName, cmd->sAccel);
}

Action * Command::createAction(void)
{
    Action *pcAction;
    pcAction = new Action(this,getMainWindow());
    if (auto pixmap = getPixmap())
        setActionIcon(pcAction, Gui::BitmapFactory().iconFromTheme(pixmap));
    applyCommandData(this->className(), pcAction);
    return pcAction;
}

void Command::refreshIcon()
{
    if (auto group = qobject_cast<ActionGroup*>(_pcAction)) {
        for (auto qaction : group->actions()) {
            if (auto action = qobject_cast<Action*>(qaction->parent())) {
                if (auto cmd = action->command())
                    cmd->refreshIcon();
            }
        }
    }
    setup(_pcAction);
    languageChange();
}

void Command::languageChange()
{
    if (_pcAction) {
        setup(_pcAction);
        applyCommandData(this->className(), _pcAction);
    }
}

void Command::setActionIcon(Action *action, const QIcon &icon)
{
    action->setIcon(icon);
}

void Command::setup(Action *pcAction) {
    if (!pcAction)
        return;
    if (auto qaction = _getChildQAction(pcAction)) {
        if (auto action = qobject_cast<Action*>(qaction->parent())) {
            if (auto cmd = action->command()) {
                pcAction->setText(QCoreApplication::translate(className(), getMenuText()));
                setActionIcon(pcAction, commandIcon(cmd));
                const char *context = dynamic_cast<PythonCommand*>(cmd) ? cmd->getName() : cmd->className();
                const char *tooltip = cmd->getToolTipText();
                const char *statustip = cmd->getStatusTip();
                if (!statustip || '\0' == *statustip)
                    statustip = tooltip;
                pcAction->setToolTip(QCoreApplication::translate(context,tooltip),
                        QCoreApplication::translate(cmd->className(), cmd->getMenuText()));
                pcAction->setStatusTip(QCoreApplication::translate(context,statustip));
            } else {
                pcAction->setText(action->text());
                setActionIcon(pcAction, action->icon());
                pcAction->setToolTip(action->toolTip());
                pcAction->setStatusTip(action->statusTip());
            }
        } else {
            setActionIcon(_pcAction, qaction->icon());
            _pcAction->setChecked(qaction->isChecked(),true);
            _pcAction->setText(qaction->text());
            _pcAction->setToolTip(qaction->toolTip());
            _pcAction->setStatusTip(qaction->statusTip());
        }
        return;
    }
    if (auto pixmap = getPixmap())
        setActionIcon(pcAction, Gui::BitmapFactory().iconFromTheme(pixmap));
}

void Command::updateAction(int mode)
{
    if (auto group = qobject_cast<ActionGroup*>(_pcAction)) {
        for (auto qaction : group->actions()) {
            if (auto action = qobject_cast<Action*>(qaction->parent())) {
                if (auto cmd = action->command())
                    cmd->updateAction(mode);
            }
        }
    }
    setup(_pcAction);
    languageChange();
}


//===========================================================================
// GroupCommand
//===========================================================================

GroupCommand::GroupCommand(const char *name,
                           int defaultAction,
                           const char *paramPath,
                           const char *paramEntry)
    :Command(name)
{
    _defaultAction = defaultAction;
    if (paramPath && *paramPath != '\0' && paramEntry && *paramEntry != '\0') {
        _hEntry = paramEntry;
        _hParam = App::GetApplication().GetParameterGroupByPath(paramPath);
        _hParam->Attach(this);
    }
}

GroupCommand::~GroupCommand()
{
    if (_hParam)
        _hParam->Detach(this);
}

int GroupCommand::addCommand(Command *cmd, bool reg) {
    cmds.emplace_back(cmd,cmds.size());
    if(cmd && reg)
        Application::Instance->commandManager().addCommand(cmd);
    return (int)cmds.size()-1;
}

Command *GroupCommand::addCommand(const char *name) {
    auto cmd = Application::Instance->commandManager().getCommandByName(name);
    if(cmd)
        addCommand(cmd,false);
    else
        FC_ERR("Command not found " << name);
    return cmd;
}

Command *GroupCommand::getCommand(int idx) const
{
    if (idx >= 0 && idx < (int)cmds.size())
        return cmds[idx].first;
    return nullptr;
}

std::vector<Command*> GroupCommand::getCommands() const
{
    std::vector<Command*> res;
    for (const auto &v : cmds) {
        if (v.first)
            res.push_back(v.first);
    }
    return res;
}

Action * GroupCommand::createAction(void) {
    ActionGroup* pcAction = new ActionGroup(this, getMainWindow());
    pcAction->setMenuRole(QAction::NoRole);
    pcAction->setDropDownMenu(true);
    pcAction->setExclusive(false);
    pcAction->setCheckable(true);
    pcAction->setWhatsThis(QString::fromUtf8(sWhatsThis));

    int idx = _defaultAction;
    if (_hParam)
        _hParam->GetInt(_hEntry.c_str(), _defaultAction);

    int i=-1;
    for(auto &v : cmds) {
        ++i;
        if(!v.first)
            pcAction->addAction(QStringLiteral(""))->setSeparator(true);
        else {
            v.first->addToGroup(pcAction);
            if(idx<0 && !(v.first->getType() & NoDefaultAction))
                idx = i;
        }
    }

    if (idx >= 0 && idx < (int)cmds.size()) {
        pcAction->setProperty("defaultAction", idx);
        pcAction->setChecked(cmds[idx].first->getAction()->isChecked(),true);
    }
    setup(pcAction);
    applyCommandData(this->className(), pcAction);
    return pcAction;
}

void GroupCommand::OnChange(Base::Subject<const char*> &, const char* sReason)
{
    if (_hParam && _pcAction && boost::equals(sReason, _hEntry)) {
        int idx = _hParam->GetInt(sReason, _defaultAction);
        if (_pcAction->property("defaultAction").toInt() != idx) {
            _pcAction->setProperty("defaultAction", idx);
            languageChange();
        }
    }
}

void GroupCommand::activated(int iMsg)
{
    Command::activated(iMsg);
    if (_hParam)
        _hParam->SetInt(_hEntry.c_str(), iMsg);
}

//===========================================================================
// CheckableCommand
//===========================================================================

CheckableCommand::CheckableCommand(const char *name)
    :Command(name)
{}

void CheckableCommand::activated(int iMsg)
{
    auto checked = !!iMsg;
    setOption(checked);
    if(_pcAction) _pcAction->setChecked(checked,true);
}

bool CheckableCommand::isActive(void)
{
    bool checked = getOption();
    if(_pcAction && _pcAction->isChecked()!=checked)
        _pcAction->setChecked(checked,true);
    return true;
}

Gui::Action * CheckableCommand::createAction(void)
{
    Action *pcAction = Command::createAction();
    pcAction->setCheckable(true);
    _pcAction = pcAction;
    isActive();
    return pcAction;
}

void CheckableCommand::setActionIcon(Action *, const QIcon &)
{
    // Do not set icon, as it looks bad in menu for checkable
}

//===========================================================================
// MacroCommand
//===========================================================================

/* TRANSLATOR Gui::MacroCommand */

MacroCommand::MacroCommand(const char* name, bool system, bool preselect)
  : Command(StringCache::New(name))
  , systemMacro(system)
  , preselect(preselect)
{
    sGroup = preselect ? "Macros Preselection" : "Macros";
    eType  = 0;
    sScriptName = nullptr;
    if (preselect) {
        bCanLog = false;
        eType |= NoHistory | NoTransaction | NoUpdateActions;
    }
}

MacroCommand::~MacroCommand()
{
}

void MacroCommand::activated(int iMsg)
{
    if (preselect) {
        if (iMsg == 0 || iMsg == 1) {
            auto checked = !!iMsg;
            setOption(checked);
            if(_pcAction) _pcAction->setChecked(checked,true);
            return;
        }
        if (iMsg != 2)
            return;
    }

    QDir d;
    if (!systemMacro) {
        std::string cMacroPath;

        cMacroPath = App::GetApplication().GetParameterGroupByPath
                             ("User parameter:BaseApp/Preferences/Macro")->GetASCII("MacroPath",
                                     App::Application::getUserMacroDir().c_str());

        d = QDir(QString::fromUtf8(cMacroPath.c_str()));
    }
    else {
        QString dirstr = QString::fromUtf8(App::GetApplication().getHomePath()) + QString::fromUtf8("Macro");
        d = QDir(dirstr);
    }

    QFileInfo fi(d, QString::fromUtf8(sScriptName));
    if (!fi.exists()) {
        QMessageBox::critical(Gui::getMainWindow(),
            qApp->translate("Gui::MacroCommand", "Macro file doesn't exist"),
            qApp->translate("Gui::MacroCommand", "No such macro file: '%1'").arg(fi.absoluteFilePath()));
    }
    else {
        Application::Instance->macroManager()->run(MacroManager::File, fi.filePath().toUtf8());
        // after macro run recalculate the document
        if (Application::Instance->activeDocument())
            Application::Instance->activeDocument()->getDocument()->recompute();
    }
}

Action * MacroCommand::createAction(void)
{
    Action *pcAction;
    pcAction = new Action(this,getMainWindow());
    pcAction->setMenuRole(QAction::NoRole);
    pcAction->setText(QString::fromUtf8(sMenuText));
    pcAction->setToolTip(QString::fromUtf8(sToolTipText));
    pcAction->setStatusTip(QString::fromUtf8(sStatusTip));
    if (pcAction->statusTip().isEmpty())
        pcAction->setStatusTip(pcAction->toolTip());
    pcAction->setWhatsThis(QString::fromUtf8(sWhatsThis));
    if (sPixmap)
        pcAction->setIcon(Gui::BitmapFactory().pixmap(sPixmap));

    if (preselect) {
        pcAction->setCheckable(true);
        isActive();
    }

    return pcAction;
}

bool MacroCommand::isActive(void)
{
    if (preselect) {
        bool checked = getOption();
        if(_pcAction && _pcAction->isChecked()!=checked)
            _pcAction->setChecked(checked,true);
    }
    return true;
}


void MacroCommand::setScriptName( const char* s )
{
    this->sScriptName = StringCache::New(s);
}

void MacroCommand::load()
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Macro");

    if (hGrp->HasGroup("Macros")) {
        hGrp = hGrp->GetGroup("Macros");
        std::vector<Base::Reference<ParameterGrp> > macros = hGrp->GetGroups();
        for (std::vector<Base::Reference<ParameterGrp> >::iterator it = macros.begin(); it!=macros.end(); ++it ) {
            bool preselect = (*it)->GetBool("Preselect", false);
            bool system = (*it)->GetBool("System", false);
            MacroCommand* macro = new MacroCommand((*it)->GetGroupName(), system, preselect);
            macro->setScriptName  ( (*it)->GetASCII( "Script"     ).c_str() );
            macro->setMenuText    ( (*it)->GetASCII( "Menu"       ).c_str() );
            macro->setToolTipText ( (*it)->GetASCII( "Tooltip"    ).c_str() );
            macro->setWhatsThis   ( (*it)->GetASCII( "WhatsThis"  ).c_str() );
            macro->setStatusTip   ( (*it)->GetASCII( "Statustip"  ).c_str() );
            if ((*it)->GetASCII("Pixmap", "nix") != "nix")
                macro->setPixmap    ( (*it)->GetASCII( "Pixmap"     ).c_str() );
            macro->setAccel       ( (*it)->GetASCII( "Accel",0    ).c_str() );
            Application::Instance->commandManager().addCommand( macro );
        }
    }
}

void MacroCommand::save()
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Macro")->GetGroup("Macros");
    hGrp->Clear();

    std::vector<Command*> macros = Application::Instance->commandManager().getGroupCommands("Macros");
    if ( macros.size() > 0 ) {
        for (std::vector<Command*>::iterator it = macros.begin(); it!=macros.end(); ++it ) {
            MacroCommand* macro = (MacroCommand*)(*it);
            ParameterGrp::handle hMacro = hGrp->GetGroup(macro->getName());
            hMacro->SetASCII( "Script",    macro->getScriptName () );
            hMacro->SetASCII( "Menu",      macro->getMenuText   () );
            hMacro->SetASCII( "Tooltip",   macro->getToolTipText() );
            hMacro->SetASCII( "WhatsThis", macro->getWhatsThis  () );
            hMacro->SetASCII( "Statustip", macro->getStatusTip  () );
            hMacro->SetASCII( "Pixmap",    macro->getPixmap     () );
            hMacro->SetASCII( "Accel",     macro->getAccel      () );
            hMacro->SetBool( "System",     macro->systemMacro );
            hMacro->SetBool( "Preselect",  macro->preselect );
        }
    }
}

void MacroCommand::setOption(bool checked)
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
              "User parameter:BaseApp/Preferences/Commands");
    hGrp->SetBool(getName(), checked);
}

bool MacroCommand::getOption() const
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
              "User parameter:BaseApp/Preferences/Commands");
    return hGrp->GetBool(getName(), false);
}

//===========================================================================
// PythonCommand
//===========================================================================

PythonCommand::PythonCommand(const char* name, PyObject * pcPyCommand, const char* pActivationString)
  : Command(StringCache::New(name))
  ,_pcPyCommand(pcPyCommand)
{
    if (pActivationString)
        Activation = pActivationString;

    sGroup = "Python";

    Py_INCREF(_pcPyCommand);

    // call the method "GetResources()" of the command object
    _pcPyResourceDict = Interpreter().runMethodObject(_pcPyCommand, "GetResources");
    // check if the "GetResources()" method returns a Dict object
    if (!PyDict_Check(_pcPyResourceDict)) {
        throw Base::TypeError("PythonCommand::PythonCommand(): Method GetResources() of the Python "
                              "command object returns the wrong type (has to be dict)");
    }

    // check for command type
    std::string cmdType = getResource("CmdType");
    if (!cmdType.empty()) {
        int type = 0;
        if (cmdType.find("AlterDoc") != std::string::npos)
            type += int(AlterDoc);
        if (cmdType.find("Alter3DView") != std::string::npos)
            type += int(Alter3DView);
        if (cmdType.find("AlterSelection") != std::string::npos)
            type += int(AlterSelection);
        if (cmdType.find("ForEdit") != std::string::npos)
            type += int(ForEdit);
        if (cmdType.find("NoTransaction") != std::string::npos)
            type += int(NoTransaction);
        if (cmdType.find("NoDefaultAction") != std::string::npos)
            type += int(NoDefaultAction);
        if (cmdType.find("NoHistory") != std::string::npos)
            type += int(NoHistory);
        eType = type;
    }
}

PythonCommand::~PythonCommand()
{
    Base::PyGILStateLocker lock;
    Py_DECREF(_pcPyCommand);
    Py_DECREF(_pcPyResourceDict);
}

const char* PythonCommand::getResource(const char* sName) const
{
    Base::PyGILStateLocker lock;
    PyObject* pcTemp;

    // get the "MenuText" resource string
    pcTemp = PyDict_GetItemString(_pcPyResourceDict,sName);
    if (!pcTemp)
        return "";
    if (!PyUnicode_Check(pcTemp)) {
        throw Base::TypeError("PythonCommand::getResource(): Method GetResources() of the Python "
                              "command object returns a dictionary which holds not only strings");
    }
    return PyUnicode_AsUTF8(pcTemp);
}

void PythonCommand::activated(int iMsg)
{
    if (Activation.empty()) {
        try {
            if (isCheckable()) {
                Interpreter().runMethod(_pcPyCommand, "Activated", "", 0, "(i)", iMsg);
            }
            else {
                Interpreter().runMethodVoid(_pcPyCommand, "Activated");
            }
        }
        catch (const Base::PyException& e) {
            Base::Console().Error("Running the Python command '%s' failed:\n%s\n%s",
                                  sName, e.getStackTrace().c_str(), e.what());
        }
        catch (const Base::Exception&) {
            Base::Console().Error("Running the Python command '%s' failed, try to resume",sName);
        }
    }
    else {
        runCommand(Doc,Activation.c_str());
    }
}

bool PythonCommand::isActive(void)
{
    try {
        Base::PyGILStateLocker lock;
        Py::Object cmd(_pcPyCommand);
        if (cmd.hasAttr("IsActive")) {
            Py::Callable call(cmd.getAttr("IsActive"));
            Py::Tuple args;
            Py::Object ret = call.apply(args);
            // if return type is not boolean or not true
            if (!PyBool_Check(ret.ptr()) || ret.ptr() != Py_True)
                return false;
        }
    }
    catch(Py::Exception& e) {
        Base::PyGILStateLocker lock;
        e.clear();
        return false;
    }

    return true;
}

void PythonCommand::languageChange()
{
    if (_pcAction) {
        Base::PyGILStateLocker lock;
        Py_DECREF(_pcPyResourceDict);
        _pcPyResourceDict = Interpreter().runMethodObject(_pcPyCommand, "GetResources");
        applyCommandData(getName(), _pcAction);
    }
}

const char* PythonCommand::getHelpUrl(void) const
{
    PyObject* pcTemp;
    pcTemp = Interpreter().runMethodObject(_pcPyCommand, "CmdHelpURL");
    if (! pcTemp )
        return "";
    if (! PyUnicode_Check(pcTemp) )
        throw Base::TypeError("PythonCommand::CmdHelpURL(): Method CmdHelpURL() of the Python command object returns no string");
    return PyUnicode_AsUTF8(pcTemp);
}

Action * PythonCommand::createAction(void)
{
    QAction* qtAction = new QAction(0);
    Action *pcAction;

    pcAction = new Action(this, qtAction, getMainWindow());
    if (boost::equals(getName(), "Part_JoinConnect"))
        FC_LOG("found");
    if (auto pixmap = getPixmap())
        pcAction->setIcon(Gui::BitmapFactory().iconFromTheme(pixmap));
    applyCommandData(this->getName(), pcAction);

    try {
        if (isCheckable()) {
            pcAction->setCheckable(true);
            // Here the QAction must be tmp. blocked to avoid to call the 'activated' method
            qtAction->blockSignals(true);
            pcAction->setChecked(isChecked());
            qtAction->blockSignals(false);
        }
    }
    catch (const Base::Exception& e) {
        Base::Console().Error("%s\n", e.what());
    }

    return pcAction;
}

const char* PythonCommand::getWhatsThis() const
{
    const char* whatsthis = getResource("WhatsThis");
    if (!whatsthis || whatsthis[0] == '\0')
        whatsthis = this->getName();
    return whatsthis;
}

const char* PythonCommand::getMenuText() const
{
    return getResource("MenuText");
}

const char* PythonCommand::getToolTipText() const
{
    return getResource("ToolTip");
}

const char* PythonCommand::getStatusTip() const
{
    return getResource("StatusTip");
}

const char* PythonCommand::getPixmap() const
{
    const char* ret = getResource("Pixmap");
    return (ret && ret[0] != '\0') ? ret : 0;
}

const char* PythonCommand::getAccel() const
{
    return getResource("Accel");
}

bool PythonCommand::isCheckable() const
{
    Base::PyGILStateLocker lock;
    PyObject* item = PyDict_GetItemString(_pcPyResourceDict,"Checkable");
    return item ? true : false;
}

bool PythonCommand::isChecked() const
{
    PyObject* item = PyDict_GetItemString(_pcPyResourceDict,"Checkable");
    if (!item) {
        throw Base::ValueError("PythonCommand::isChecked(): Method GetResources() of the Python "
                               "command object doesn't contain the key 'Checkable'");
    }

    if (PyBool_Check(item)) {
        return PyObject_IsTrue(item) ? true : false;
    }
    else {
        throw Base::ValueError("PythonCommand::isChecked(): Method GetResources() of the Python "
                               "command object contains the key 'Checkable' which is not a boolean");
    }
}

//===========================================================================
// PythonGroupCommand
//===========================================================================

PythonGroupCommand::PythonGroupCommand(const char* name, PyObject * pcPyCommand)
  : Command(StringCache::New(name))
  ,_pcPyCommand(pcPyCommand)
{
    sGroup = "Python";

    Py_INCREF(_pcPyCommand);

    // call the method "GetResources()" of the command object
    _pcPyResource = Interpreter().runMethodObject(_pcPyCommand, "GetResources");
    // check if the "GetResources()" method returns a Dict object
    if (!PyDict_Check(_pcPyResource)) {
        throw Base::TypeError("PythonGroupCommand::PythonGroupCommand(): Method GetResources() of the Python "
                              "command object returns the wrong type (has to be dict)");
    }

    // check for command type
    std::string cmdType = getResource("CmdType");
    if (!cmdType.empty()) {
        int type = 0;
        if (cmdType.find("AlterDoc") != std::string::npos)
            type += int(AlterDoc);
        if (cmdType.find("Alter3DView") != std::string::npos)
            type += int(Alter3DView);
        if (cmdType.find("AlterSelection") != std::string::npos)
            type += int(AlterSelection);
        if (cmdType.find("ForEdit") != std::string::npos)
            type += int(ForEdit);
        eType = type;
    }
}

PythonGroupCommand::~PythonGroupCommand()
{
    Base::PyGILStateLocker lock;
    Py_DECREF(_pcPyCommand);
}

Action * PythonGroupCommand::createAction(void)
{
    Gui::ActionGroup* pcAction = new Gui::ActionGroup(this, Gui::getMainWindow());
    pcAction->setDropDownMenu(hasDropDownMenu());
    pcAction->setExclusive(isExclusive());

    applyCommandData(this->getName(), pcAction);

    int defaultId = 0;

    try {
        Base::PyGILStateLocker lock;
        Py::Object cmd(_pcPyCommand);
        Gui::CommandManager &rcCmdMgr = Gui::Application::Instance->commandManager();

        Py::Callable call(cmd.getAttr("GetCommands"));
        Py::Sequence args;
        Py::Sequence ret(call.apply(args));
        for (auto it = ret.begin(); it != ret.end(); ++it) {
            Py::String str(*it);
            QByteArray cmdName = static_cast<std::string>(str).c_str();
            if (auto cmd = rcCmdMgr.getCommandByName(cmdName.constData())) {
                cmd->initAction();
                cmd->getAction()->action()->setProperty("CommandName", cmdName);
                pcAction->addAction(cmd->getAction()->action());
            }
        }

        if (cmd.hasAttr("GetDefaultCommand")) {
            Py::Callable call2(cmd.getAttr("GetDefaultCommand"));
            Py::Int def(call2.apply(args));
            defaultId = static_cast<int>(def);
        }

        QList<QAction*> a = pcAction->actions();
        if (defaultId >= 0 && defaultId < a.size()) {
            QAction* qtAction = a[defaultId];
            if (qtAction->isCheckable()) {
                // if the command is 'exclusive' then activate the default action
                if (pcAction->isExclusive()) {
                    qtAction->blockSignals(true);
                    qtAction->setChecked(true);
                    qtAction->blockSignals(false);
                }else if(qtAction->isCheckable()){
                    pcAction->setCheckable(true);
                    pcAction->setChecked(qtAction->isChecked(),true);
                }
            }
        }
    }
    catch(Py::Exception&) {
        Base::PyGILStateLocker lock;
        Base::PyException e;
        Base::Console().Error("createAction() of the Python command '%s' failed:\n%s\n%s",
                              sName, e.getStackTrace().c_str(), e.what());
    }

    _pcAction = pcAction;
    languageChange();
    return pcAction;
}

const char* PythonGroupCommand::getHelpUrl(void) const
{
    return "";
}

const char* PythonGroupCommand::getResource(const char* sName) const
{
    Base::PyGILStateLocker lock;
    PyObject* pcTemp;

    // get the "MenuText" resource string
    pcTemp = PyDict_GetItemString(_pcPyResource, sName);
    if (!pcTemp)
        return "";
    if (!PyUnicode_Check(pcTemp)) {
        throw Base::ValueError("PythonGroupCommand::getResource(): Method GetResources() of the Python "
                               "group command object returns a dictionary which holds not only strings");
    }
    return PyUnicode_AsUTF8(pcTemp);
}

const char* PythonGroupCommand::getWhatsThis() const
{
    const char* whatsthis = getResource("WhatsThis");
    if (!whatsthis || whatsthis[0] == '\0')
        whatsthis = this->getName();
    return whatsthis;
}

const char* PythonGroupCommand::getMenuText() const
{
    return getResource("MenuText");
}

const char* PythonGroupCommand::getToolTipText() const
{
    return getResource("ToolTip");
}

const char* PythonGroupCommand::getStatusTip() const
{
    return getResource("StatusTip");
}

const char* PythonGroupCommand::getPixmap() const
{
    const char* ret = getResource("Pixmap");
    return (ret && ret[0] != '\0') ? ret : 0;
}

const char* PythonGroupCommand::getAccel() const
{
    return getResource("Accel");
}

bool PythonGroupCommand::isExclusive() const
{
    PyObject* item = PyDict_GetItemString(_pcPyResource,"Exclusive");
    if (!item) {
        return false;
    }

    if (PyBool_Check(item)) {
        return PyObject_IsTrue(item) ? true : false;
    }
    else {
        throw Base::TypeError("PythonGroupCommand::isExclusive(): Method GetResources() of the Python "
                              "command object contains the key 'Exclusive' which is not a boolean");
    }
}

bool PythonGroupCommand::hasDropDownMenu() const
{
    PyObject* item = PyDict_GetItemString(_pcPyResource,"DropDownMenu");
    if (!item) {
        return true;
    }

    if (PyBool_Check(item)) {
        return PyObject_IsTrue(item) ? true : false;
    }
    else {
        throw Base::TypeError("PythonGroupCommand::hasDropDownMenu(): Method GetResources() of the Python "
                              "command object contains the key 'DropDownMenu' which is not a boolean");
    }
}

//===========================================================================
// Std_MacroPreselectCommands
//===========================================================================

class CmdMacroPreselectCommands: public Command
{
public:
    CmdMacroPreselectCommands()
        : Command("Std_MacroPreselectionCommands")
    {
        sGroup      = "Macros Preselection";
        eType       = NoHistory;
        bCanLog     = false;
        sMenuText     = QT_TR_NOOP("Preselection Macros");
        sToolTipText  = QT_TR_NOOP("List of macros that are invoked on 3D view pre-selection");
        sWhatsThis    = "Std_MacroPreselectionCommands";
        sStatusTip    = sToolTipText;
        sAccel        = "M, P";
    }

    virtual const char* className() const {return "CmdMacroPreselectCommands";}

    void add(Command *cmd) {
        if (!boost::starts_with(cmd->getName(), "Std_Macro_Presel"))
            return;
        auto cmdpresel = dynamic_cast<MacroCommand*>(cmd);
        if (cmdpresel && cmdpresel->isPreselectionMacro()
                      && cmdset.insert(cmd).second)
        {
            cmd->addTo(&_menu);
        }
    }

    void remove(Command *cmd) {
        if (cmdset.erase(cmd))
            _menu.removeAction(cmd->getAction()->action());
    }

    Action * createAction(void) {
        Action * action = Command::createAction();
        action->action()->setMenu(&_menu);
        return action;
    }

    static CmdMacroPreselectCommands *instance()
    {
        static CmdMacroPreselectCommands *_instance;
        if (!_instance)
            _instance = new CmdMacroPreselectCommands;
        return _instance;
    }

    virtual void activated(int iMsg)
    {
        (void)iMsg;
        PieMenu::exec(&_menu, QCursor::pos(), getName());
    }

private:
    std::set<Command *> cmdset;
    QMenu _menu;
};

//===========================================================================
// CommandManager
//===========================================================================

CommandManager::CommandManager()
{
    auto cmd = CmdMacroPreselectCommands::instance();
    _sCommands[cmd->getName()] = cmd;
}

CommandManager::~CommandManager()
{
    clearCommands();

    // In case of unregistering Python callback
    Base::PyGILStateLocker lock;
    _CallbackMap.clear();
    _Callbacks.clear();
}

int CommandManager::registerCallback(const CallbackFunction & func, const char *cmd)
{
    if (!cmd)
        cmd = "";
    
    _Callbacks[++_CallbackId] = _CallbackMap.emplace(cmd, func);
    return _CallbackId;
}

bool CommandManager::unregisterCallback(int id)
{
    auto it = _Callbacks.find(id);
    if (it == _Callbacks.end())
        return false;
    // In case of unregistering Python callback
    Base::PyGILStateLocker lock;
    _CallbackMap.erase(it->second);
    _Callbacks.erase(it);
    return true;
}

static int CommandInvokeCount;
struct CommandInvokeCounter
{
    CommandInvokeCounter()
    {
        ++CommandInvokeCount;
    }
    ~CommandInvokeCounter()
    {
        --CommandInvokeCount;
    }
};

bool CommandManager::onInvokeCommand(const char *cmd, int i) const
{
    CommandInvokeCounter counter;
    if (CommandInvokeCount > 10)
        return true;

    auto it = _CallbackMap.find(cmd);
    if (it == _CallbackMap.end())
        return true;
    if (cmd[0]) {
        for (; it != _CallbackMap.end(); ++it) {
            if (it->first != cmd)
                break;
            if (!it->second(cmd, i))
                return false;
        }
    }
    it = _CallbackMap.find("");
    for (; it != _CallbackMap.end(); ++it) {
        if (!it->first.empty())
            break;
        if (!it->second(cmd, i))
            return false;
    }
    return true;
}

void CommandManager::addCommand(Command* pCom)
{
    auto &cmd = _sCommands[pCom->getName()];
    if (cmd) {
        if(FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
            FC_ERR("duplicate command " << pCom->getName());
        return;
    }
    ++_revision;
    cmd = pCom;

    const char *workbench = Application::Instance->initializingWorkbench();
    static ParameterGrp::handle hGrp;
    if (!hGrp)
        hGrp = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/Commands");
    if (workbench)
        hGrp->SetASCII(pCom->getName(), workbench);
    else
        hGrp->RemoveASCII(pCom->getName());
    CmdMacroPreselectCommands::instance()->add(pCom);
    signalChanged();
}

void CommandManager::removeCommand(Command* pCom)
{
    if (pCom == CmdMacroPreselectCommands::instance())
        return;
    std::map <std::string,Command*>::iterator It = _sCommands.find(pCom->getName());
    if (It != _sCommands.end()) {
        CmdMacroPreselectCommands::instance()->remove(pCom);
        ++_revision;
        delete It->second;
        _sCommands.erase(It);
        signalChanged();
    }
}

void CommandManager::clearCommands()
{
    _sCommands.erase(CmdMacroPreselectCommands::instance()->getName());
    for ( std::map<std::string,Command*>::iterator it = _sCommands.begin(); it != _sCommands.end(); ++it )
        delete it->second;
    _sCommands.clear();
    ++_revision;
    signalChanged();
}

bool CommandManager::addTo(const char* Name, QWidget *pcWidget)
{
    Command *cmd = getCommandByName(Name);
    if (!cmd) {
        // Print in release mode only a log message instead of an error message to avoid to annoy the user
#ifdef FC_DEBUG
        Base::Console().Error("CommandManager::addTo() try to add an unknown command (%s) to a widget!\n",Name);
#else
        Base::Console().Warning("Unknown command '%s'\n",Name);
#endif
        return false;
    }

    cmd->addTo(pcWidget);
    return true;
}

std::vector <Command*> CommandManager::getModuleCommands(const char *sModName) const
{
    std::vector <Command*> vCmds;

    for ( std::map<std::string, Command*>::const_iterator It= _sCommands.begin();It!=_sCommands.end();++It) {
        if ( strcmp(It->second->getAppModuleName(),sModName) == 0)
            vCmds.push_back(It->second);
    }

    return vCmds;
}

std::vector <Command*> CommandManager::getAllCommands(void) const
{
    std::vector <Command*> vCmds;

    for ( std::map<std::string, Command*>::const_iterator It= _sCommands.begin();It!=_sCommands.end();++It) {
        vCmds.push_back(It->second);
    }

    return vCmds;
}

void CommandManager::refreshIcons()
{
    for (const auto &v : _sCurrentCommandModes)
        updateCommands(v.first.c_str(), v.second);
    for (auto &v : _sCommands)
        v.second->refreshIcon();
}

std::vector <Command*> CommandManager::getGroupCommands(const char *sGrpName) const
{
    std::vector <Command*> vCmds;

    for ( std::map<std::string, Command*>::const_iterator It= _sCommands.begin();It!=_sCommands.end();++It) {
        if ( strcmp(It->second->getGroupName(),sGrpName) == 0)
            vCmds.push_back(It->second);
    }

    return vCmds;
}

Command* CommandManager::getCommandByName(const char* sName) const
{
    std::map<std::string,Command*>::const_iterator it = _sCommands.find( sName );
    if ( it == _sCommands.end() ) {
        static ParameterGrp::handle hGrp;
        if (!hGrp)
            hGrp = App::GetApplication().GetParameterGroupByPath(
                    "User parameter:BaseApp/Preferences/Commands");
        auto workbench = hGrp->GetASCII(sName);
        if (workbench.empty())
            return nullptr;
        if (!_sPendingWorkbench.insert(workbench).second) {
            FC_LOG("Recursive loading of command '" << sName << "' from workbench " << workbench);
            return nullptr;
        }
        FC_LOG("Initializing workbench " << workbench << " due to command " << sName);
        Application::Instance->initializeWorkbench(workbench.c_str());
        _sPendingWorkbench.erase(workbench);
        it = _sCommands.find(sName);
        if (it == _sCommands.end()) {
            hGrp->RemoveASCII(sName);
            return nullptr;
        }
    }
    return it->second;
}

void CommandManager::runCommandByName (const char* sName, int iMsg) const
{
    Command* pCmd = getCommandByName(sName);

    if (pCmd)
        pCmd->invoke(iMsg);
}

void CommandManager::testActive(void)
{
    for ( std::map<std::string, Command*>::iterator It= _sCommands.begin();It!=_sCommands.end();++It) {
        It->second->testActive();
    }
}

void CommandManager::addCommandMode(const char* sContext, const char* sName)
{
    _sCommandModes[sContext].push_back(sName);
}

void CommandManager::updateCommands(const char* sContext, int mode)
{
    _sCurrentCommandModes[sContext] = mode;
    std::map<std::string, std::list<std::string> >::iterator it = _sCommandModes.find(sContext);
    int rev = _revision;
    if (it != _sCommandModes.end()) {
        for (std::list<std::string>::iterator jt = it->second.begin(); jt != it->second.end(); ++jt) {
            Command* cmd = getCommandByName(jt->c_str());
            if (cmd) {
                cmd->updateAction(mode);
                ++_revision;
            }
        }
    }
    if (rev != _revision)
        signalChanged();
}

const Command* Gui::CommandManager::checkAcceleratorForConflicts(const char* accel, const Command* ignore) const
{
    if (!accel || accel[0] == '\0')
        return nullptr;

    QString newCombo = QString::fromUtf8(accel);
    if (newCombo.isEmpty())
        return nullptr;
    auto newSequence = QKeySequence::fromString(newCombo);
    if (newSequence.count() == 0)
        return nullptr;

    // Does this command shortcut conflict with other commands already defined?
    auto commands = Application::Instance->commandManager().getAllCommands();
    for (const auto& cmd : commands) {
        if (cmd == ignore)
            continue;
        auto existingAccel = cmd->getAccel();
        if (!existingAccel || existingAccel[0] == '\0')
            continue;

        // Three possible conflict scenarios:
        // 1) Exactly the same combo as another command
        // 2) The new command is a one-char combo that overrides an existing two-char combo
        // 3) The old command is a one-char combo that overrides the new command

        QString existingCombo = QString::fromUtf8(existingAccel);
        if (existingCombo.isEmpty())
            continue;
        auto existingSequence = QKeySequence::fromString(existingCombo);
        if (existingSequence.count() == 0)
            continue;

        // Exact match
        if (existingSequence == newSequence)
            return cmd;

        // If it's not exact, then see if one of the sequences is a partial match for
        // the beginning of the other sequence
        auto numCharsToCheck = std::min(existingSequence.count(), newSequence.count());
        bool firstNMatch = true;
        for (int i = 0; i < numCharsToCheck; ++i) {
            if (newSequence[i] != existingSequence[i]) {
                firstNMatch = false;
                break;
            }
        }
        if (firstNMatch)
            return cmd;
    }

    return nullptr;
}

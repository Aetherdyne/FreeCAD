/***************************************************************************
 *   Copyright (c) Juergen Riegel         <juergen.riegel@web.de>          *
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
# include <assert.h>
# include <string>
# include <boost/signals.hpp>
# include <boost/bind.hpp>
# include <QApplication>
# include <QString>
# include <QStatusBar>
#endif

/// Here the FreeCAD includes sorted by Base,App,Gui......
#include "Application.h"
#include "Document.h"
#include "Selection.h"
#include "SelectionFilter.h"
#include "View3DInventor.h"
#include <Base/Exception.h>
#include <Base/Console.h>
#include <Base/Interpreter.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObjectPy.h>
#include <App/GeoFeature.h>
#include <Gui/SelectionObjectPy.h>
#include "MainWindow.h"
#include "ViewProviderDocumentObject.h"

FC_LOG_LEVEL_INIT("Selection",false,true,true)


using namespace Gui;
using namespace std;

SelectionObserver::SelectionObserver(bool attach,int resolve)
    :resolve(resolve)
{
    if(attach)
        attachSelection();
}

SelectionObserver::~SelectionObserver()
{
    detachSelection();
}

bool SelectionObserver::blockConnection(bool block)
{
    if(!isConnectionAttached())
        return false;

    bool ok = connectSelection.blocked();
    if (block)
        connectSelection.block();
    else
        connectSelection.unblock();
    return ok;
}

bool SelectionObserver::isConnectionBlocked() const
{
    return connectSelection.blocked();
}

bool SelectionObserver::isConnectionAttached() const
{
    return connectSelection.connected();
}

void SelectionObserver::attachSelection()
{
    if (!connectSelection.connected()) {
        auto &signal = resolve>1?Selection().signalSelectionChanged3:(
                resolve?Selection().signalSelectionChanged2:
                Selection().signalSelectionChanged);
        connectSelection = signal.connect(boost::bind
            (&SelectionObserver::onSelectionChanged, this, _1));
    }
}

void SelectionObserver::detachSelection()
{
    if (connectSelection.connected()) {
        connectSelection.disconnect();
    }
}

// -------------------------------------------

std::vector<SelectionObserverPython*> SelectionObserverPython::_instances;

SelectionObserverPython::SelectionObserverPython(const Py::Object& obj, int resolve) 
    : SelectionObserver(true,resolve),inst(obj)
{
}

SelectionObserverPython::~SelectionObserverPython()
{
}

void SelectionObserverPython::addObserver(const Py::Object& obj, int resolve)
{
    _instances.push_back(new SelectionObserverPython(obj,resolve));
}

void SelectionObserverPython::removeObserver(const Py::Object& obj)
{
    SelectionObserverPython* obs=0;
    for (std::vector<SelectionObserverPython*>::iterator it =
        _instances.begin(); it != _instances.end(); ++it) {
        if ((*it)->inst == obj) {
            obs = *it;
            _instances.erase(it);
            break;
        }
    }

    delete obs;
}

void SelectionObserverPython::onSelectionChanged(const SelectionChanges& msg)
{
    switch (msg.Type)
    {
    case SelectionChanges::AddSelection:
        addSelection(msg);
        break;
    case SelectionChanges::RmvSelection:
        removeSelection(msg);
        break;
    case SelectionChanges::SetSelection:
        setSelection(msg);
        break;
    case SelectionChanges::ClrSelection:
        clearSelection(msg);
        break;
    case SelectionChanges::SetPreselect:
        setPreselection(msg);
        break;
    case SelectionChanges::RmvPreselect:
        removePreselection(msg);
        break;
    case SelectionChanges::PickedListChanged:
        pickedListChanged();
        break;
    default:
        break;
    }
}

void SelectionObserverPython::pickedListChanged()
{
    Base::PyGILStateLocker lock;
    try {
        if (this->inst.hasAttr(std::string("pickedListChanged"))) {
            Py::Callable method(this->inst.getAttr(std::string("pickedListChanged")));
            Py::Tuple args;
            method.apply(args);
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

void SelectionObserverPython::addSelection(const SelectionChanges& msg)
{
    Base::PyGILStateLocker lock;
    try {
        if (this->inst.hasAttr(std::string("addSelection"))) {
            Py::Callable method(this->inst.getAttr(std::string("addSelection")));
            Py::Tuple args(4);
            args.setItem(0, Py::String(msg.pDocName ? msg.pDocName : ""));
            args.setItem(1, Py::String(msg.pObjectName ? msg.pObjectName : ""));
            args.setItem(2, Py::String(msg.pSubName ? msg.pSubName : ""));
            Py::Tuple tuple(3);
            tuple[0] = Py::Float(msg.x);
            tuple[1] = Py::Float(msg.y);
            tuple[2] = Py::Float(msg.z);
            args.setItem(3, tuple);
            method.apply(args);
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

void SelectionObserverPython::removeSelection(const SelectionChanges& msg)
{
    Base::PyGILStateLocker lock;
    try {
        if (this->inst.hasAttr(std::string("removeSelection"))) {
            Py::Callable method(this->inst.getAttr(std::string("removeSelection")));
            Py::Tuple args(3);
            args.setItem(0, Py::String(msg.pDocName ? msg.pDocName : ""));
            args.setItem(1, Py::String(msg.pObjectName ? msg.pObjectName : ""));
            args.setItem(2, Py::String(msg.pSubName ? msg.pSubName : ""));
            method.apply(args);
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

void SelectionObserverPython::setSelection(const SelectionChanges& msg)
{
    Base::PyGILStateLocker lock;
    try {
        if (this->inst.hasAttr(std::string("setSelection"))) {
            Py::Callable method(this->inst.getAttr(std::string("setSelection")));
            Py::Tuple args(1);
            args.setItem(0, Py::String(msg.pDocName ? msg.pDocName : ""));
            method.apply(args);
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

void SelectionObserverPython::clearSelection(const SelectionChanges& msg)
{
    Base::PyGILStateLocker lock;
    try {
        if (this->inst.hasAttr(std::string("clearSelection"))) {
            Py::Callable method(this->inst.getAttr(std::string("clearSelection")));
            Py::Tuple args(1);
            args.setItem(0, Py::String(msg.pDocName ? msg.pDocName : ""));
            method.apply(args);
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

void SelectionObserverPython::setPreselection(const SelectionChanges& msg)
{
    Base::PyGILStateLocker lock;
    try {
        if (this->inst.hasAttr(std::string("setPreselection"))) {
            Py::Callable method(this->inst.getAttr(std::string("setPreselection")));
            Py::Tuple args(3);
            args.setItem(0, Py::String(msg.pDocName ? msg.pDocName : ""));
            args.setItem(1, Py::String(msg.pObjectName ? msg.pObjectName : ""));
            args.setItem(2, Py::String(msg.pSubName ? msg.pSubName : ""));
            method.apply(args);
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

void SelectionObserverPython::removePreselection(const SelectionChanges& msg)
{
    Base::PyGILStateLocker lock;
    try {
        if (this->inst.hasAttr(std::string("removePreselection"))) {
            Py::Callable method(this->inst.getAttr(std::string("removePreselection")));
            Py::Tuple args(3);
            args.setItem(0, Py::String(msg.pDocName ? msg.pDocName : ""));
            args.setItem(1, Py::String(msg.pObjectName ? msg.pObjectName : ""));
            args.setItem(2, Py::String(msg.pSubName ? msg.pSubName : ""));
            method.apply(args);
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

// -------------------------------------------

bool SelectionSingleton::hasSelection() const
{
    return !_SelList.empty();
}

std::vector<SelectionSingleton::SelObj> SelectionSingleton::getCompleteSelection(int resolve) const
{
    return getSelection("*",resolve);
}

std::vector<SelectionSingleton::SelObj> SelectionSingleton::getSelection(const char* pDocName, 
        int resolve, bool single) const
{
    std::vector<SelObj> temp;
    if(single) temp.reserve(1);
    SelObj tempSelObj;

    App::Document *pcDoc = 0;
    if(!pDocName || strcmp(pDocName,"*")!=0) {
        pcDoc = getDocument(pDocName);
        if (!pcDoc)
            return temp;
    }

    std::map<App::DocumentObject*,std::set<std::string> > objMap;

    for(auto &sel : _SelList) {
        if(!sel.pDoc) continue;
        const char *subelement = 0;
        auto obj = getObjectOfType(sel,App::DocumentObject::getClassTypeId(),resolve,&subelement);
        if(!obj || (pcDoc && sel.pObject->getDocument()!=pcDoc))
            continue;

        // In case we are resolving objects, make sure no duplicates
        if(resolve && !objMap[obj].insert(std::string(subelement?subelement:"")).second)
            continue;

        if(single && temp.size()) {
            temp.clear();
            break;
        }

        tempSelObj.DocName  = obj->getDocument()->getName();
        tempSelObj.FeatName = obj->getNameInDocument();
        tempSelObj.SubName = subelement;
        tempSelObj.TypeName = obj->getTypeId().getName();
        tempSelObj.pObject  = obj;
        tempSelObj.pDoc     = obj->getDocument();
        tempSelObj.x        = sel.x;
        tempSelObj.y        = sel.y;
        tempSelObj.z        = sel.z;

        temp.push_back(tempSelObj);
    }

    return temp;
}

bool SelectionSingleton::hasSelection(const char* doc, bool resolve) const
{
    App::Document *pcDoc = 0;
    if(!doc || strcmp(doc,"*")!=0) {
        pcDoc = getDocument(doc);
        if (!pcDoc)
            return false;
    }
    for(auto &sel : _SelList) {
        if(!sel.pDoc) continue;
        auto obj = getObjectOfType(sel,App::DocumentObject::getClassTypeId(),resolve);
        if(obj && (!pcDoc || sel.pObject->getDocument()==pcDoc)) {
            return true;
        }
    }

    return false;
}

std::vector<SelectionSingleton::SelObj> SelectionSingleton::getPickedList(const char* pDocName) const
{
    std::vector<SelObj> temp;
    SelObj tempSelObj;

    App::Document *pcDoc = 0;
    if(!pDocName || strcmp(pDocName,"*")!=0) {
        pcDoc = getDocument(pDocName);
        if (!pcDoc)
            return temp;
    }

    for(std::list<_SelObj>::const_iterator It = _PickedList.begin();It != _PickedList.end();++It) {
        if (!pcDoc || It->pDoc == pcDoc) {
            tempSelObj.DocName  = It->DocName.c_str();
            tempSelObj.FeatName = It->FeatName.c_str();
            tempSelObj.SubName  = It->SubName.c_str();
            tempSelObj.TypeName = It->TypeName.c_str();
            tempSelObj.pObject  = It->pObject;
            tempSelObj.pDoc     = It->pDoc;
            tempSelObj.x        = It->x;
            tempSelObj.y        = It->y;
            tempSelObj.z        = It->z;
            temp.push_back(tempSelObj);
        }
    }

    return temp;
}

std::vector<SelectionObject> SelectionSingleton::getSelectionEx(
        const char* pDocName, Base::Type typeId, int resolve, bool single) const {
    return getObjectList(pDocName,typeId,_SelList,resolve,single);
}

std::vector<SelectionObject> SelectionSingleton::getPickedListEx(const char* pDocName, Base::Type typeId) const {
    return getObjectList(pDocName,typeId,_PickedList,false);
}

std::vector<SelectionObject> SelectionSingleton::getObjectList(const char* pDocName, Base::Type typeId,
        std::list<_SelObj> &objList, int resolve, bool single) const
{
    std::vector<SelectionObject> temp;
    if(single) temp.reserve(1);
    std::map<App::DocumentObject*,size_t> SortMap;

    // check the type
    if (typeId == Base::Type::badType())
        return temp;

    App::Document *pcDoc = 0;
    if(!pDocName || strcmp(pDocName,"*")!=0) {
        pcDoc = getDocument(pDocName);
        if (!pcDoc)
            return temp;
    }

    for (auto &sel : objList) {
        if(!sel.pDoc) continue;
        const char *subelement = 0;
        auto obj = getObjectOfType(sel,typeId,resolve,&subelement);
        if(!obj || (pcDoc && sel.pObject->getDocument()!=pcDoc))
            continue;
        auto it = SortMap.find(obj);
        if(it!=SortMap.end()) {
            // only add sub-element
            if (subelement && *subelement) {
                if(resolve && !temp[it->second]._SubNameSet.insert(subelement).second)
                    continue;
                temp[it->second].SubNames.push_back(subelement);
                temp[it->second].SelPoses.push_back(Base::Vector3d(sel.x,sel.y,sel.z));
            }
        }
        else {
            if(single && temp.size()) {
                temp.clear();
                break;
            }
            // create a new entry
            temp.emplace_back(obj);
            if (subelement && *subelement) {
                temp.back().SubNames.push_back(subelement);
                temp.back().SelPoses.push_back(Base::Vector3d(sel.x,sel.y,sel.z));
                if(resolve)
                    temp.back()._SubNameSet.insert(subelement);
            }
            SortMap.insert(std::make_pair(obj,temp.size()-1));
        }
    }

    return temp;
}

bool SelectionSingleton::needPickedList() const {
    return _needPickedList;
}

void SelectionSingleton::enablePickedList(bool enable) {
    if(enable != _needPickedList) {
        _needPickedList = enable;
        _PickedList.clear();
        SelectionChanges Chng;
        Chng.Type      = SelectionChanges::PickedListChanged;
        Notify(Chng);
        signalSelectionChanged(Chng);
    }
}

bool SelectionSingleton::hasPickedList() const {
    return _PickedList.size();
}

int SelectionSingleton::getAsPropertyLinkSubList(App::PropertyLinkSubList &prop) const
{
    std::vector<Gui::SelectionObject> sel = this->getSelectionEx();
    std::vector<App::DocumentObject*> objs; objs.reserve(sel.size()*2);
    std::vector<std::string> subs; subs.reserve(sel.size()*2);
    for (std::size_t iobj = 0; iobj < sel.size(); iobj++) {
        Gui::SelectionObject &selitem = sel[iobj];
        App::DocumentObject* obj = selitem.getObject();
        const std::vector<std::string> &subnames = selitem.getSubNames();
        if (subnames.size() == 0){//whole object is selected
            objs.push_back(obj);
            subs.push_back(std::string());
        } else {
            for (std::size_t isub = 0; isub < subnames.size(); isub++) {
                objs.push_back(obj);
                subs.push_back(subnames[isub]);
            }
        }
    }
    assert(objs.size()==subs.size());
    prop.setValues(objs, subs);
    return objs.size();
}

App::DocumentObject *SelectionSingleton::getObjectOfType(_SelObj &sel, 
        Base::Type typeId, int resolve, const char **subelement)
{
    auto obj = sel.pObject;
    if(!obj || !obj->getNameInDocument())
        return 0;
    const char *subname = sel.SubName.c_str();
    if(resolve) {
        if(!sel.resolved) {
            sel.resolved = true;
            std::pair<std::string,std::string> elementName;
            sel.pResolvedObject = App::GeoFeature::resolveElement(obj,subname,elementName);
            sel.newElementName = elementName.first;
            sel.oldElementName = elementName.second;
            if(sel.newElementName.empty())
                sel.newElementName = sel.oldElementName;
        }
        obj = sel.pResolvedObject;
        if(resolve>1)
            subname = sel.newElementName.c_str();
        else
            subname = sel.oldElementName.c_str();
    }
    if(!obj || !obj->isDerivedFrom(typeId))
        return 0;
    if(subelement) *subelement = subname;
    return obj;
}

vector<App::DocumentObject*> SelectionSingleton::getObjectsOfType(const Base::Type& typeId, const char* pDocName, int resolve) const
{
    std::vector<App::DocumentObject*> temp;

    App::Document *pcDoc = 0;
    if(!pDocName || strcmp(pDocName,"*")!=0) {
        pcDoc = getDocument(pDocName);
        if (!pcDoc)
            return temp;
    }

    std::set<App::DocumentObject*> objs;
    for(auto &sel : _SelList) {
        if(pcDoc && pcDoc!=sel.pDoc) continue;
        App::DocumentObject *pObject = getObjectOfType(sel,typeId,resolve);
        if (pObject) {
            auto ret = objs.insert(pObject);
            if(ret.second)
                temp.push_back(pObject);
        }
    }

    return temp;
}

std::vector<App::DocumentObject*> SelectionSingleton::getObjectsOfType(const char* typeName, const char* pDocName, int resolve) const
{
    Base::Type typeId = Base::Type::fromName(typeName);
    if (typeId == Base::Type::badType())
        return std::vector<App::DocumentObject*>();
    return getObjectsOfType(typeId, pDocName, resolve);
}

unsigned int SelectionSingleton::countObjectsOfType(const Base::Type& typeId, const char* pDocName, bool resolve) const
{
    unsigned int iNbr=0;
    App::Document *pcDoc = 0;
    if(!pDocName || strcmp(pDocName,"*")!=0) {
        pcDoc = getDocument(pDocName);
        if (!pcDoc)
            return 0;
    }

    for (auto &sel : _SelList) {
        if((!pcDoc||pcDoc==sel.pDoc) && getObjectOfType(sel,typeId,resolve))
            iNbr++;
    }

    return iNbr;
}

unsigned int SelectionSingleton::countObjectsOfType(const char* typeName, const char* pDocName, bool resolve) const
{
    Base::Type typeId = Base::Type::fromName(typeName);
    if (typeId == Base::Type::badType())
        return 0;
    return countObjectsOfType(typeId, pDocName, resolve);
}


void SelectionSingleton::slotSelectionChanged(const SelectionChanges& msg) {
    if(msg.Type == SelectionChanges::SetPreselectSignal ||
       msg.Type == SelectionChanges::ShowSelection ||
       msg.Type == SelectionChanges::HideSelection)
        return;
    
    if(msg.pDocName && 
       msg.pObjectName && msg.pObjectName[0] &&
       msg.pSubName && msg.pSubName[0])
    {
        App::Document* pDoc = getDocument(msg.pDocName);
        if(!pDoc) return;
        std::pair<std::string,std::string> elementName;
        auto &newElementName = elementName.first;
        auto &oldElementName = elementName.second;
        auto pObject = App::GeoFeature::resolveElement(
                pDoc->getObject(msg.pObjectName),msg.pSubName,elementName);
        if (!pObject) return;
        std::string docName(pObject->getDocument()->getName());
        std::string objName(pObject->getNameInDocument());
        SelectionChanges msg2(msg);
        msg2.pDocName = docName.c_str();
        msg2.pObjectName = objName.c_str();
        msg2.pSubName = newElementName.size()?newElementName.c_str():oldElementName.c_str();
        signalSelectionChanged3(msg2);

        msg2.pSubName = oldElementName.c_str();
        signalSelectionChanged2(msg2);

    }else {
        signalSelectionChanged3(msg);
        signalSelectionChanged2(msg);
    }
}

bool SelectionSingleton::setPreselect(const char* pDocName, const char* pObjectName, const char* pSubName, float x, float y, float z, bool signal)
{
    if (DocName != "")
        rmvPreselect();

    if (ActiveGate && !signal) {
        App::Document* pDoc = getDocument(pDocName);
        if (!pDoc || !pObjectName) 
            return false;
        std::pair<std::string,std::string> elementName;
        auto &newElementName = elementName.first;
        auto &oldElementName = elementName.second;
        auto pObject = App::GeoFeature::resolveElement(
                pDoc->getObject(pObjectName),pSubName,elementName);
        if (!pObject)
            return false;

        const char *subelement = pSubName;
        if(gateResolve > 1)
            subelement = newElementName.size()?newElementName.c_str():oldElementName.c_str();
        else if(gateResolve)
            subelement = oldElementName.c_str();
        if (!ActiveGate->allow(pObject->getDocument(),pObject,subelement)) {
            QString msg;
            if (ActiveGate->notAllowedReason.length() > 0){
                msg = QObject::tr(ActiveGate->notAllowedReason.c_str());
            } else {
                msg = QCoreApplication::translate("SelectionFilter","Not allowed:");
            }
            msg.append(
                        QObject::tr(" %1.%2.%3 ")
                        .arg(QString::fromLatin1(pDocName))
                        .arg(QString::fromLatin1(pObjectName))
                        .arg(QString::fromLatin1(pSubName))
                        );

            if (getMainWindow()) {
                getMainWindow()->showMessage(msg);
                Gui::MDIView* mdi = Gui::Application::Instance->activeDocument()->getActiveView();
                mdi->setOverrideCursor(QCursor(Qt::ForbiddenCursor));
            }
            return false;
        }

    }

    DocName = pDocName;
    FeatName= pObjectName;
    SubName = pSubName;
    hx = x;
    hy = y;
    hz = z;

    // set up the change object
    SelectionChanges Chng;
    Chng.pDocName  = DocName.c_str();
    Chng.pObjectName = FeatName.c_str();
    Chng.pSubName  = SubName.c_str();
    Chng.pTypeName = "";
    Chng.x = x;
    Chng.y = y;
    Chng.z = z;

    if(!signal)
        Chng.Type = SelectionChanges::SetPreselect;
    else
        Chng.Type = SelectionChanges::SetPreselectSignal; // signal 3D view to do the highlight

    Notify(Chng);
    signalSelectionChanged(Chng);

    // allows the preselection
    return true;
}

void SelectionSingleton::setPreselectCoord( float x, float y, float z)
{
    static char buf[513];

    // if nothing is in preselect ignore
    if(!CurrentPreselection.pObjectName) return;

    CurrentPreselection.x = x;
    CurrentPreselection.y = y;
    CurrentPreselection.z = z;

    snprintf(buf,512,"Preselected: %s.%s.%s (%f,%f,%f)",CurrentPreselection.pDocName
                                                       ,CurrentPreselection.pObjectName
                                                       ,CurrentPreselection.pSubName
                                                       ,x,y,z);

    if (getMainWindow())
        getMainWindow()->showMessage(QString::fromLatin1(buf));
}

void SelectionSingleton::rmvPreselect()
{
    if (DocName == "")
        return;

    SelectionChanges Chng;
    Chng.pDocName  = DocName.c_str();
    Chng.pObjectName = FeatName.c_str();
    Chng.pSubName  = SubName.c_str();
    Chng.Type = SelectionChanges::RmvPreselect;

    // reset the current preselection
    CurrentPreselection.pDocName =0;
    CurrentPreselection.pObjectName = 0;
    CurrentPreselection.pSubName = 0;
    CurrentPreselection.x = 0.0;
    CurrentPreselection.y = 0.0;
    CurrentPreselection.z = 0.0;

    // notify observing objects
    Notify(Chng);
    signalSelectionChanged(Chng);

    DocName = "";
    FeatName= "";
    SubName = "";
    hx = 0;
    hy = 0;
    hz = 0;

    if (ActiveGate && getMainWindow()) {
        Gui::MDIView* mdi = Gui::Application::Instance->activeDocument()->getActiveView();
        mdi->restoreOverrideCursor();
    }

    //Base::Console().Log("Sel : Rmv preselect \n");
}

const SelectionChanges &SelectionSingleton::getPreselection(void) const
{
    return CurrentPreselection;
}

// add a SelectionGate to control what is selectable
void SelectionSingleton::addSelectionGate(Gui::SelectionGate *gate, int resolve)
{
    if (ActiveGate)
        rmvSelectionGate();
    
    ActiveGate = gate;
    gateResolve = resolve;
}

// remove the active SelectionGate
void SelectionSingleton::rmvSelectionGate(void)
{
    if (ActiveGate) {
        delete ActiveGate;
        ActiveGate=0;
        Gui::Document* doc = Gui::Application::Instance->activeDocument();
        if (doc) {
            Gui::MDIView* mdi = doc->getActiveView();
            mdi->restoreOverrideCursor();
        }
    }
}


App::Document* SelectionSingleton::getDocument(const char* pDocName) const
{
    if (pDocName && pDocName[0])
        return App::GetApplication().getDocument(pDocName);
    else
        return App::GetApplication().getActiveDocument();
}

bool SelectionSingleton::addSelection(const char* pDocName, const char* pObjectName, const char* pSubName, float x, float y, float z, const std::vector<SelObj> *pickedList)
{
    if(pickedList) {
        _PickedList.clear();
        for(const auto &sel : *pickedList) {
            _PickedList.push_back(_SelObj());
            auto &s = _PickedList.back();
            s.DocName = sel.DocName;
            s.FeatName = sel.FeatName;
            s.SubName = sel.SubName;
            s.TypeName = sel.TypeName;
            s.pObject = sel.pObject;
            s.pDoc = sel.pDoc;
            s.x = sel.x;
            s.y = sel.y;
            s.z = sel.z;
        }
        SelectionChanges Chng;
        Chng.Type      = SelectionChanges::PickedListChanged;
        Notify(Chng);
        signalSelectionChanged(Chng);
    }

    // already in ?
    if (isSelected(pDocName, pObjectName, pSubName))
        return true;

    _SelObj temp;

    temp.pDoc = getDocument(pDocName);

    if (temp.pDoc) {
        if(pObjectName)
            temp.pObject = temp.pDoc->getObject(pObjectName);
        else
            temp.pObject = 0;

        temp.DocName  = pDocName;
        temp.FeatName = pObjectName ? pObjectName : "";
        temp.SubName  = pSubName ? pSubName : "";
        temp.x        = x;
        temp.y        = y;
        temp.z        = z;

        if (temp.pObject)
            temp.TypeName = temp.pObject->getTypeId().getName();

        // check for a Selection Gate
        if (ActiveGate) {
            const char *subelement = 0;
            auto pObject = getObjectOfType(temp,App::DocumentObject::getClassTypeId(),gateResolve,&subelement);
            if (!ActiveGate->allow(pObject?pObject->getDocument():temp.pDoc,pObject,subelement)) {
                if (getMainWindow()) {
                    QString msg;
                    if (ActiveGate->notAllowedReason.length() > 0) {
                        msg = QObject::tr(ActiveGate->notAllowedReason.c_str());
                    } else {
                        msg = QCoreApplication::translate("SelectionFilter","Selection not allowed by filter");
                    }
                    getMainWindow()->showMessage(msg);
                    Gui::MDIView* mdi = Gui::Application::Instance->activeDocument()->getActiveView();
                    mdi->setOverrideCursor(Qt::ForbiddenCursor);
                }
                ActiveGate->notAllowedReason.clear();
                QApplication::beep();
                return false;
            }
        }

        _SelList.push_back(temp);

        SelectionChanges Chng;

        Chng.pDocName  = pDocName;
        Chng.pObjectName = pObjectName ? pObjectName : "";
        Chng.pSubName  = pSubName ? pSubName : "";
        Chng.pTypeName = temp.TypeName.c_str();
        Chng.x         = x;
        Chng.y         = y;
        Chng.z         = z;
        Chng.Type      = SelectionChanges::AddSelection;

        FC_LOG("Add Selection "<<
                (pDocName?pDocName:"(null)")<<'.'<<
                (pObjectName?pObjectName:"(null)")<<'.' <<
                (pSubName?pSubName:"(null)") <<
                " ("<<x<<", "<<y<<", "<<z<<')');

        Notify(Chng);
        FC_TRACE("signaling add selection");
        signalSelectionChanged(Chng);
        FC_TRACE("done signaling add selection");

        getMainWindow()->updateActions();

        // allow selection
        return true;
    }
    else {
        // neither an existing nor active document available
        // this can often happen when importing .iv files
        Base::Console().Error("Cannot add to selection: no document '%s' found.\n", pDocName);
        return false;
    }
}

bool SelectionSingleton::addSelection(const char* pDocName, const char* pObjectName, const std::vector<std::string>& pSubNames)
{
    if(_PickedList.size()) {
        _PickedList.clear();
        SelectionChanges Chng;
        Chng.Type = SelectionChanges::PickedListChanged;
        Notify(Chng);
        signalSelectionChanged(Chng);
    }

    _SelObj temp;

    temp.pDoc = getDocument(pDocName);

    if (temp.pDoc) {
        if(pObjectName)
            temp.pObject = temp.pDoc->getObject(pObjectName);
        else
            temp.pObject = 0;

        if (temp.pObject)
            temp.TypeName = temp.pObject->getTypeId().getName();

        temp.DocName  = pDocName;
        temp.FeatName = pObjectName ? pObjectName : "";
        for (std::vector<std::string>::const_iterator it = pSubNames.begin(); it != pSubNames.end(); ++it) {
            temp.SubName  = it->c_str();
            temp.x        = 0;
            temp.y        = 0;
            temp.z        = 0;

            _SelList.push_back(temp);
        }

        SelectionChanges Chng;

        Chng.pDocName  = pDocName;
        Chng.pObjectName = pObjectName ? pObjectName : "";
        Chng.pSubName  = "";
        Chng.pTypeName = temp.TypeName.c_str();
        Chng.x         = 0;
        Chng.y         = 0;
        Chng.z         = 0;
        Chng.Type      = SelectionChanges::AddSelection;

        FC_TRACE("notifying add selection");
        Notify(Chng);
        FC_TRACE("signaling add selection");
        signalSelectionChanged(Chng);
        FC_TRACE("done signaling add selection");

        getMainWindow()->updateActions();

        // allow selection
        return true;
    }
    else {
        // neither an existing nor active document available 
        // this can often happen when importing .iv files
        Base::Console().Error("Cannot add to selection: no document '%s' found.\n", pDocName);
        return false;
    }
}

bool SelectionSingleton::updateSelection(bool show, const char* pDocName, 
                            const char* pObjectName, const char* pSubName)
{
    if(!pDocName || !pObjectName)
        return false;
    if (!isSelected(pDocName, pObjectName, pSubName))
        return false;
    auto pDoc = getDocument(pDocName);
    if(!pDoc) return false;
    auto pObject = pDoc->getObject(pObjectName);
    if(!pObject) return false;

    SelectionChanges Chng;
    Chng.pDocName  = pDocName;
    Chng.pObjectName = pObjectName;
    Chng.pSubName  = pSubName ? pSubName : "";
    Chng.pTypeName = pObject->getTypeId().getName();
    Chng.x         = 0;
    Chng.y         = 0;
    Chng.z         = 0;
    Chng.Type      = show?SelectionChanges::ShowSelection:SelectionChanges::HideSelection;

    FC_LOG("Update Selection "<<pDocName << '.' << pObjectName << '.' 
            << (pSubName?pSubName:"(null)"));

    FC_TRACE("signaling update selection");
    Notify(Chng);
    signalSelectionChanged(Chng);
    FC_TRACE("done signaling update selection");
    return true;
}


void SelectionSingleton::rmvSelection(const char* pDocName, const char* pObjectName, const char* pSubName, 
        const std::vector<SelObj> *pickedList)
{
    if(pickedList) {
        _PickedList.clear();
        for(const auto &sel : *pickedList) {
            _PickedList.push_back(_SelObj());
            auto &s = _PickedList.back();
            s.DocName = sel.DocName;
            s.FeatName = sel.FeatName;
            s.SubName = sel.SubName;
            s.TypeName = sel.TypeName;
            s.pObject = sel.pObject;
            s.pDoc = sel.pDoc;
            s.x = sel.x;
            s.y = sel.y;
            s.z = sel.z;
        }
        SelectionChanges Chng;
        Chng.Type      = SelectionChanges::PickedListChanged;
        Notify(Chng);
        signalSelectionChanged(Chng);
    }

    std::vector<SelectionChanges> rmvList;

    if(!pDocName) return;

    size_t len = pSubName?std::strlen(pSubName):0;

    for(auto It=_SelList.begin(),ItNext=It;It!=_SelList.end();It=ItNext) {
        ++ItNext;
        if(It->DocName != pDocName) continue;
        // if no object name is specified, remove all objects
        if(pObjectName && *pObjectName && It->FeatName!=pObjectName) continue;
        // if no subname is specified, remove all subobjects of the matching object
        if(len) {
            // otherwise, match subojects with common prefix, separated by '.'
            if(std::strncmp(It->SubName.c_str(),pSubName,len)!=0 ||
               (It->SubName.length()!=len && It->SubName[len-1]!='.'))
                continue;
        }

        // save in tmp. string vars
        std::string tmpDocName = It->DocName;
        std::string tmpFeaName = It->FeatName;
        std::string tmpSubName = It->SubName;
        std::string tmpTypName = It->TypeName;

        // destroy the _SelObj item
        It = _SelList.erase(It);

        SelectionChanges Chng;
        Chng.pDocName  = tmpDocName.c_str();
        Chng.pObjectName = tmpFeaName.c_str();
        Chng.pSubName  = tmpSubName.c_str();
        Chng.pTypeName = tmpTypName.c_str();
        Chng.Type      = SelectionChanges::RmvSelection;

        FC_LOG("Rmv Selection "<<
            (pDocName?pDocName:"(null)")<<'.'<<
            (pObjectName?pObjectName:"(null)")<<'.' <<
            (pSubName?pSubName:"(null)"));

        Notify(Chng);
        signalSelectionChanged(Chng);
    
        rmvList.push_back(Chng);

        getMainWindow()->updateActions();
    }
}

void SelectionSingleton::setVisible(int visible) {
    std::set<std::pair<App::DocumentObject*,App::DocumentObject*> > filter;
    if(visible<0) 
        visible = -1;
    else if(visible>0)
        visible = 1;
    for(auto &sel : _SelList) {
        if(sel.DocName.empty() || sel.FeatName.empty() || !sel.pObject) 
            continue;
        // get parent object
        App::DocumentObject *parent = 0;
        std::string elementName;
        auto obj = sel.pObject->resolve(sel.SubName.c_str(),&parent,&elementName);
        if(!obj || !obj->getNameInDocument() || (parent && !parent->getNameInDocument()))
            continue;
        // try call parent object's setElementVisibility
        if(parent) {
            // prevent setting the same object visibility more than once
            if(!filter.insert(std::make_pair(obj,parent)).second)
                continue;

            int vis = parent->isElementVisible(elementName.c_str());
            if(vis>=0) {
                if(vis>0) vis = 1;
                if(visible>=0) {
                    if(vis == visible)
                        continue;
                    vis = visible;
                }else
                    vis = !vis;

                if(!vis)
                    updateSelection(false,sel.DocName.c_str(),sel.FeatName.c_str(), sel.SubName.c_str());
                parent->setElementVisible(elementName.c_str(),vis?true:false);
                if(vis)
                    updateSelection(true,sel.DocName.c_str(),sel.FeatName.c_str(), sel.SubName.c_str());
                continue;
            }

            // Fall back to direct object visibility setting
        }

        if(!filter.insert(std::make_pair(obj,(App::DocumentObject*)0)).second)
            continue;

        auto vp = Application::Instance->getViewProvider(obj);
        if(vp) {
            int vis;
            if(visible>=0)
                vis = visible;
            else
                vis = !vp->isShow();

            if(vis) {
                vp->show();
                updateSelection(vis,sel.DocName.c_str(),sel.FeatName.c_str(), sel.SubName.c_str());
            } else {
                updateSelection(vis,sel.DocName.c_str(),sel.FeatName.c_str(), sel.SubName.c_str());
                vp->hide();
            }
        }
    }
}

void SelectionSingleton::setSelection(const char* pDocName, const std::vector<App::DocumentObject*>& sel)
{
    if(_PickedList.size()) {
        _PickedList.clear();
        SelectionChanges Chng;
        Chng.Type      = SelectionChanges::PickedListChanged;
        Notify(Chng);
        signalSelectionChanged(Chng);
    }

    App::Document *pcDoc;
    pcDoc = getDocument(pDocName);
    if (!pcDoc)
        return;

    std::set<App::DocumentObject*> cur_sel, new_sel;
    new_sel.insert(sel.begin(), sel.end());

    // Make sure to keep the order of the currently selected objects
    std::list<_SelObj> temp;
    for (std::list<_SelObj>::const_iterator it = _SelList.begin(); it != _SelList.end(); ++it) {
        if (it->pDoc != pcDoc)
            temp.push_back(*it);
        else {
            cur_sel.insert(it->pObject);
            if (new_sel.find(it->pObject) != new_sel.end())
                temp.push_back(*it);
        }
    }

    // Get the objects we must add to the selection
    std::vector<App::DocumentObject*> diff_new_cur;
    std::back_insert_iterator< std::vector<App::DocumentObject*> > biit(diff_new_cur);
    std::set_difference(new_sel.begin(), new_sel.end(), cur_sel.begin(), cur_sel.end(), biit);

    _SelObj obj;
    for (std::vector<App::DocumentObject*>::const_iterator it = diff_new_cur.begin(); it != diff_new_cur.end(); ++it) {
        obj.pDoc = pcDoc;
        obj.pObject = *it;
        obj.DocName = pDocName;
        obj.FeatName = (*it)->getNameInDocument();
        obj.SubName = "";
        obj.TypeName = (*it)->getTypeId().getName();
        obj.x = 0.0f;
        obj.y = 0.0f;
        obj.z = 0.0f;
        temp.push_back(obj);
    }

    if (cur_sel == new_sel) // nothing has changed
        return;

    _SelList = temp;

    SelectionChanges Chng;
    Chng.Type = SelectionChanges::SetSelection;
    Chng.pDocName = pDocName;
    Chng.pObjectName = "";
    Chng.pSubName = "";
    Chng.pTypeName = "";

    Notify(Chng);
    signalSelectionChanged(Chng);
    getMainWindow()->updateActions();
}

void SelectionSingleton::clearSelection(const char* pDocName)
{
    // Because the introduction of external editing, it is best to make
    // clearSelection(0) behave as clearCompleteSelection(), which is the same
    // behavior of python Selection.clearSelection(None)
    if(!pDocName || !pDocName[0] || strcmp(pDocName,"*")==0) {
        clearCompleteSelection();
        return;
    }

    if(_PickedList.size()) {
        _PickedList.clear();
        SelectionChanges Chng;
        Chng.Type      = SelectionChanges::PickedListChanged;
        Notify(Chng);
        signalSelectionChanged(Chng);
    }

    App::Document* pDoc;
    pDoc = getDocument(pDocName);
    if(pDoc) {
        std::string docName;
        if (pDocName)
            docName = pDocName;
        else
            docName = pDoc->getName(); // active document

        if(DocName == docName)
            rmvPreselect();

        std::list<_SelObj> selList;
        for (std::list<_SelObj>::iterator it = _SelList.begin(); it != _SelList.end(); ++it) {
            if (it->DocName != docName)
                selList.push_back(*it);
        }

        _SelList = selList;

        SelectionChanges Chng;
        Chng.Type = SelectionChanges::ClrSelection;
        Chng.pDocName = docName.c_str();
        Chng.pObjectName = "";
        Chng.pSubName = "";
        Chng.pTypeName = "";

        FC_TRACE("notifying clear selection");
        Notify(Chng);
        FC_TRACE("signaling clear selection");
        signalSelectionChanged(Chng);
        FC_TRACE("done signaling clear selection");

        getMainWindow()->updateActions();
    }
}

void SelectionSingleton::clearCompleteSelection()
{
    if(_PickedList.size()) {
        _PickedList.clear();
        SelectionChanges Chng;
        Chng.Type      = SelectionChanges::PickedListChanged;
        Notify(Chng);
        signalSelectionChanged(Chng);
    }

    rmvPreselect();

    _SelList.clear();

    SelectionChanges Chng;
    Chng.Type = SelectionChanges::ClrSelection;
    Chng.pDocName = "";
    Chng.pObjectName = "";
    Chng.pSubName = "";
    Chng.pTypeName = "";

    FC_LOG("Clear selection");

    Notify(Chng);
    signalSelectionChanged(Chng);
    getMainWindow()->updateActions();
}

bool SelectionSingleton::isSelected(const char* pDocName, const char* pObjectName, const char* pSubName) const
{
    const char* tmpDocName = pDocName ? pDocName : "";
    const char* tmpFeaName = pObjectName ? pObjectName : "";
    const char* tmpSubName = pSubName ? pSubName : "";
    for (std::list<_SelObj>::const_iterator It = _SelList.begin();It != _SelList.end();++It)
        if (It->DocName == tmpDocName && It->FeatName == tmpFeaName && It->SubName == tmpSubName)
            return true;
    return false;
}

bool SelectionSingleton::isSelected(App::DocumentObject* obj, const char* pSubName) const
{
    if (!obj) return false;

    for(list<_SelObj>::const_iterator It = _SelList.begin();It != _SelList.end();++It) {
        if (It->pObject == obj) {
            if (pSubName) {
                if (It->SubName == pSubName)
                    return true;
            }
            else {
                return true;
            }
        }
    }

    return false;
}

const char *SelectionSingleton::getSelectedElement(App::DocumentObject *obj, const char* pSubName) const 
{
    if (!obj) return 0;

    for(list<_SelObj>::const_iterator It = _SelList.begin();It != _SelList.end();++It) {
        if (It->pObject == obj) {
            auto len = It->SubName.length();
            if(!len)
                return "";
            if (pSubName && strncmp(pSubName,It->SubName.c_str(),It->SubName.length())==0){
                if(pSubName[len]==0 || pSubName[len-1] == '.')
                    return It->SubName.c_str();
            }
        }
    }
    return 0;
}

void SelectionSingleton::slotDeletedObject(const App::DocumentObject& Obj)
{
    if(!Obj.getNameInDocument()) return;

    // remove also from the selection, if selected
    Selection().rmvSelection( Obj.getDocument()->getName(), Obj.getNameInDocument() );

    if(_PickedList.size()) {
        bool changed = false;
        for(auto it=_PickedList.begin(),itNext=it;it!=_PickedList.end();it=itNext) {
            ++itNext;
            auto &sel = *it;
            if(sel.DocName == Obj.getDocument()->getName() &&
               sel.FeatName == Obj.getNameInDocument())
            {
                changed = true;
                _PickedList.erase(it);
            }
        }
        if(changed) {
            SelectionChanges Chng;
            Chng.Type      = SelectionChanges::PickedListChanged;
            Notify(Chng);
            signalSelectionChanged(Chng);
        }
    }
}


//**************************************************************************
// Construction/Destruction

/**
 * A constructor.
 * A more elaborate description of the constructor.
 */
SelectionSingleton::SelectionSingleton():_needPickedList(false)
{
    hx = 0;
    hy = 0;
    hz = 0;
    ActiveGate = 0;
    gateResolve = 1;
    App::GetApplication().signalDeletedObject.connect(boost::bind(&Gui::SelectionSingleton::slotDeletedObject, this, _1));
    CurrentPreselection.Type = SelectionChanges::ClrSelection;
    CurrentPreselection.pDocName = 0;
    CurrentPreselection.pObjectName = 0;
    CurrentPreselection.pSubName = 0;
    CurrentPreselection.pTypeName = 0;
    CurrentPreselection.x = 0.0;
    CurrentPreselection.y = 0.0;
    CurrentPreselection.z = 0.0;
    signalSelectionChanged.connect(boost::bind(&Gui::SelectionSingleton::slotSelectionChanged, this, _1));
}

/**
 * A destructor.
 * A more elaborate description of the destructor.
 */
SelectionSingleton::~SelectionSingleton()
{
}

SelectionSingleton* SelectionSingleton::_pcSingleton = NULL;

SelectionSingleton& SelectionSingleton::instance(void)
{
    if (_pcSingleton == NULL)
        _pcSingleton = new SelectionSingleton;
    return *_pcSingleton;
}

void SelectionSingleton::destruct (void)
{
    if (_pcSingleton != NULL)
        delete _pcSingleton;
    _pcSingleton = 0;
}

//**************************************************************************
// Python stuff

// SelectionSingleton Methods  // Methods structure
PyMethodDef SelectionSingleton::Methods[] = {
    {"addSelection",         (PyCFunction) SelectionSingleton::sAddSelection, METH_VARARGS,
     "addSelection(object,[string,float,float,float]) -- Add an object to the selection\n"
     "where string is the sub-element name and the three floats represent a 3d point"},
    {"updateSelection",      (PyCFunction) SelectionSingleton::sUpdateSelection, METH_VARARGS,
     "updateSelection(show,object,[string]) -- update an object in the selection\n"
     "where string is the sub-element name and the three floats represent a 3d point"},
    {"removeSelection",      (PyCFunction) SelectionSingleton::sRemoveSelection, METH_VARARGS,
     "removeSelection(object) -- Remove an object from the selection"},
    {"clearSelection"  ,     (PyCFunction) SelectionSingleton::sClearSelection, METH_VARARGS,
     "clearSelection([string]) -- Clear the selection\n"
     "Clear the selection to the given document name. If no document is\n"
     "given the complete selection is cleared."},
    {"isSelected",           (PyCFunction) SelectionSingleton::sIsSelected, METH_VARARGS,
     "isSelected(object) -- Check if a given object is selected"},
    {"getPreselection",      (PyCFunction) SelectionSingleton::sGetPreselection, METH_VARARGS,
     "getPreselection() -- Get preselected object"},
    {"clearPreselection",   (PyCFunction) SelectionSingleton::sRemPreselection, METH_VARARGS,
     "clearPreselection() -- Clear the preselection"},
    {"countObjectsOfType",   (PyCFunction) SelectionSingleton::sCountObjectsOfType, METH_VARARGS,
     "countObjectsOfType(string, [string]) -- Get the number of selected objects\n"
     "The first argument defines the object type e.g. \"Part::Feature\" and the\n"
     "second argumeht defines the document name. If no document name is given the\n"
     "currently active document is used"},
    {"getSelection",         (PyCFunction) SelectionSingleton::sGetSelection, 1,
     "getSelection(docName=None,resolve=True,single=False) -- Return a list of selected objets\n"
     "\ndocName - document name. None means the active document, and '*' means all document"
     "\nresolve - whether to resolve the subname references."
     "\n          0: do not resolve, 1: resolve, 2: resolve with element map"
     "\nsingle - only return if there is only one selection"},
    {"preselect",            (PyCFunction) SelectionSingleton::sPreselect, 1, 
     "preselect(object,[string,float,float,float]) -- Preselect an object\n"
     "where string is the sub-element name and the three floats represent a 3d point"},
    {"getPickedList",         (PyCFunction) SelectionSingleton::sGetPickedList, 1,
     "getPickedList(docName=None) -- Return a list of objets under the last mouse click\n"
     "\ndocName - document name. None means the active document, and '*' means all document"},
    {"enablePickedList",      (PyCFunction) SelectionSingleton::sEnablePickedList, 1,
     "enablePickedList(boolean) -- Enable/disable pick list"},
    {"getCompleteSelection", (PyCFunction) SelectionSingleton::sGetCompleteSelection, 1,
     "getCompleteSelection(resolve=True) -- Return a list of selected objects of all documents."},
    {"getSelectionEx",         (PyCFunction) SelectionSingleton::sGetSelectionEx, 1,
     "getSelectionEx(docName=None,resolve=1, single=False) -- Return a list of SelectionObjects\n"
     "\ndocName - document name. None means the active document, and '*' means all document"
     "\nresolve - whether to resolve the subname references."
     "\n          0: do not resolve, 1: resolve, 2: resolve with element map"
     "\nsingle - only return if there is only one selection\n"
     "\nThe SelectionObjects contain a variety of information about the selection, e.g. sub-element names."},
    {"getSelectionObject",  (PyCFunction) SelectionSingleton::sGetSelectionObject, 1,
     "getSelectionObject(doc,obj,sub,(x,y,z)) -- Return a SelectionObject"},
    {"addObserver",         (PyCFunction) SelectionSingleton::sAddSelObserver, METH_VARARGS,
     "addObserver(Object, resolve=True) -- Install an observer\n"},
    {"removeObserver",      (PyCFunction) SelectionSingleton::sRemSelObserver, METH_VARARGS,
     "removeObserver(Object) -- Uninstall an observer\n"},
    {"addSelectionGate",      (PyCFunction) SelectionSingleton::sAddSelectionGate, 1,
     "addSelectionGate(String|Filter|Gate, resolve=True) -- activate the selection gate.\n"
     "The selection gate will prohibit all selections which do not match\n"
     "the given selection filter string.\n"
     " Examples strings are:\n"
     "'SELECT Part::Feature SUBELEMENT Edge',\n"
     "'SELECT Robot::RobotObject'\n"
     "\n"
     "You can also set an instance of SelectionFilter:\n"
     "filter = Gui.Selection.Filter('SELECT Part::Feature SUBELEMENT Edge')\n"
     "Gui.Selection.addSelectionGate(filter)\n"
     "\n"
     "And the most flexible approach is to write your own selection gate class\n"
     "that implements the method 'allow'\n"
     "class Gate:\n"
     "  def allow(self,doc,obj,sub):\n"
     "    return (sub[0:4] == 'Face')\n"
     "Gui.Selection.addSelectionGate(Gate())"},
    {"removeSelectionGate",      (PyCFunction) SelectionSingleton::sRemoveSelectionGate, METH_VARARGS,
     "removeSelectionGate() -- remove the active selection gate\n"},
    {"setVisible",            (PyCFunction) SelectionSingleton::sSetVisible, 1, 
     "setVisible(visible=None) -- set visibility of all selection items\n"
     "If 'visible' is None, then toggle visibility"},
    {NULL, NULL, 0, NULL}  /* Sentinel */
};

PyObject *SelectionSingleton::sAddSelection(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    PyObject *object;
    char* subname=0;
    float x=0,y=0,z=0;
    if (PyArg_ParseTuple(args, "O!|sfff", &(App::DocumentObjectPy::Type),&object,&subname,&x,&y,&z)) {
        App::DocumentObjectPy* docObjPy = static_cast<App::DocumentObjectPy*>(object);
        App::DocumentObject* docObj = docObjPy->getDocumentObjectPtr();
        if (!docObj || !docObj->getNameInDocument()) {
            PyErr_SetString(Base::BaseExceptionFreeCADError, "Cannot check invalid object");
            return NULL;
        }

        Selection().addSelection(docObj->getDocument()->getName(),
                                 docObj->getNameInDocument(),
                                 subname,x,y,z);
        Py_Return;
    }

    PyErr_Clear();
    PyObject *sequence;
    if (PyArg_ParseTuple(args, "O!O", &(App::DocumentObjectPy::Type),&object,&sequence)) {
        App::DocumentObjectPy* docObjPy = static_cast<App::DocumentObjectPy*>(object);
        App::DocumentObject* docObj = docObjPy->getDocumentObjectPtr();
        if (!docObj || !docObj->getNameInDocument()) {
            PyErr_SetString(Base::BaseExceptionFreeCADError, "Cannot check invalid object");
            return NULL;
        }

        try {
            if (PyTuple_Check(sequence) || PyList_Check(sequence)) {
                Py::Sequence list(sequence);
                for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
                    std::string subname = static_cast<std::string>(Py::String(*it));
                    Selection().addSelection(docObj->getDocument()->getName(),
                                             docObj->getNameInDocument(),
                                             subname.c_str());
                }

                Py_Return;
            }
        }
        catch (const Py::Exception&) {
            // do nothing here
        }
    }

    PyErr_SetString(PyExc_ValueError, "type must be 'DocumentObject[,subname[,x,y,z]]' or 'DocumentObject, list or tuple of subnames'");
    return 0;
}

PyObject *SelectionSingleton::sUpdateSelection(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    PyObject *show;
    PyObject *object;
    char* subname=0;
    if (PyArg_ParseTuple(args, "OO!|s", &show,&(App::DocumentObjectPy::Type),&object,&subname)) {
        App::DocumentObjectPy* docObjPy = static_cast<App::DocumentObjectPy*>(object);
        App::DocumentObject* docObj = docObjPy->getDocumentObjectPtr();
        if (!docObj || !docObj->getNameInDocument()) {
            PyErr_SetString(Base::BaseExceptionFreeCADError, "Cannot check invalid object");
            return NULL;
        }

        Selection().updateSelection(PyObject_IsTrue(show),
                docObj->getDocument()->getName(), docObj->getNameInDocument(), subname);
        Py_Return;
    }

    PyErr_Clear();
    PyObject *sequence;
    if (PyArg_ParseTuple(args, "O!O", &(App::DocumentObjectPy::Type),&object,&sequence)) {
        App::DocumentObjectPy* docObjPy = static_cast<App::DocumentObjectPy*>(object);
        App::DocumentObject* docObj = docObjPy->getDocumentObjectPtr();
        if (!docObj || !docObj->getNameInDocument()) {
            PyErr_SetString(Base::BaseExceptionFreeCADError, "Cannot check invalid object");
            return NULL;
        }

        try {
            if (PyTuple_Check(sequence) || PyList_Check(sequence)) {
                Py::Sequence list(sequence);
                for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
                    std::string subname = static_cast<std::string>(Py::String(*it));
                    Selection().updateSelection(docObj->getDocument()->getName(),
                                                docObj->getNameInDocument(),
                                                subname.c_str());
                }

                Py_Return;
            }
        }
        catch (const Py::Exception&) {
            // do nothing here
        }
    }

    PyErr_SetString(PyExc_ValueError, "type must be 'DocumentObject[,subname[,x,y,z]]' or 'DocumentObject, list or tuple of subnames'");
    return 0;
}


PyObject *SelectionSingleton::sPreselect(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    PyObject *object;
    char* subname=0;
    float x=0,y=0,z=0;
    if (!PyArg_ParseTuple(args, "O!|sfff", &(App::DocumentObjectPy::Type),&object,&subname,&x,&y,&z))
        return NULL;
    App::DocumentObjectPy* docObjPy = static_cast<App::DocumentObjectPy*>(object);
    App::DocumentObject* docObj = docObjPy->getDocumentObjectPtr();
    if (!docObj || !docObj->getNameInDocument()) {
        PyErr_SetString(Base::BaseExceptionFreeCADError, "Cannot check invalid object");
        return NULL;
    }
    Selection().setPreselect(docObj->getDocument()->getName(), 
            docObj->getNameInDocument(), subname,x,y,z,true);
    Py_Return;
}

PyObject *SelectionSingleton::sRemoveSelection(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    PyObject *object;
    char* subname=0;
    if (!PyArg_ParseTuple(args, "O!|s", &(App::DocumentObjectPy::Type),&object,&subname))
        return NULL;

    App::DocumentObjectPy* docObjPy = static_cast<App::DocumentObjectPy*>(object);
    App::DocumentObject* docObj = docObjPy->getDocumentObjectPtr();
    if (!docObj || !docObj->getNameInDocument()) {
        PyErr_SetString(Base::BaseExceptionFreeCADError, "Cannot check invalid object");
        return NULL;
    }

    Selection().rmvSelection(docObj->getDocument()->getName(),
                             docObj->getNameInDocument(),
                             subname);

    Py_Return;
}

PyObject *SelectionSingleton::sClearSelection(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    char *documentName=0;
    if (!PyArg_ParseTuple(args, "|s", &documentName))
        return NULL;
    documentName ? Selection().clearSelection(documentName) : Selection().clearCompleteSelection();
    Py_Return;
}

PyObject *SelectionSingleton::sIsSelected(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    PyObject *object;
    char* subname=0;
    if (!PyArg_ParseTuple(args, "O!|s", &(App::DocumentObjectPy::Type), &object, &subname))
        return NULL;

    App::DocumentObjectPy* docObj = static_cast<App::DocumentObjectPy*>(object);
    bool ok = Selection().isSelected(docObj->getDocumentObjectPtr(), subname);
    return Py_BuildValue("O", (ok ? Py_True : Py_False));
}

PyObject *SelectionSingleton::sCountObjectsOfType(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    char* objecttype;
    char* document=0;
    PyObject *resolve = Py_True;
    if (!PyArg_ParseTuple(args, "s|sO", &objecttype, &document,&resolve))
        return NULL;

    unsigned int count = Selection().countObjectsOfType(objecttype, document,PyObject_IsTrue(resolve));
#if PY_MAJOR_VERSION < 3
    return PyInt_FromLong(count);
#else
    return PyLong_FromLong(count);
#endif
}

PyObject *SelectionSingleton::sGetSelection(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    char *documentName=0;
    int resolve = 1;
    PyObject *single=Py_False;
    if (!PyArg_ParseTuple(args, "|siO", &documentName,&resolve,&single))     // convert args: Python->C 
        return NULL;                             // NULL triggers exception

    std::vector<SelectionSingleton::SelObj> sel;
    sel = Selection().getSelection(documentName,resolve,PyObject_IsTrue(single));

    try {
        Py::List list;
        for (std::vector<SelectionSingleton::SelObj>::iterator it = sel.begin(); it != sel.end(); ++it) {
            list.append(Py::asObject(it->pObject->getPyObject()));
        }
        return Py::new_reference_to(list);
    }
    catch (Py::Exception&) {
        return 0;
    }
}

PyObject *SelectionSingleton::sEnablePickedList(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    PyObject *enable = Py_True;
    if (!PyArg_ParseTuple(args, "|O", &enable))     // convert args: Python->C 
        return NULL;                             // NULL triggers exception

    Selection().enablePickedList(PyObject_IsTrue(enable));
    Py_Return;
}

PyObject *SelectionSingleton::sGetPreselection(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    const SelectionChanges& sel = Selection().getPreselection();
    SelectionObject obj(sel);
    return obj.getPyObject();
}

PyObject *SelectionSingleton::sRemPreselection(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    Selection().rmvPreselect();
    Py_Return;
}

PyObject *SelectionSingleton::sGetCompleteSelection(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    int resolve = 1;
    if (!PyArg_ParseTuple(args, "|i",&resolve))     // convert args: Python->C 
        return NULL;                             // NULL triggers exception

    std::vector<SelectionSingleton::SelObj> sel;
    sel = Selection().getCompleteSelection(resolve);

    try {
        Py::List list;
        for (std::vector<SelectionSingleton::SelObj>::iterator it = sel.begin(); it != sel.end(); ++it) {
            list.append(Py::asObject(it->pObject->getPyObject()));
        }
        return Py::new_reference_to(list);
    }
    catch (Py::Exception&) {
        return 0;
    }
}

PyObject *SelectionSingleton::sGetSelectionEx(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    char *documentName=0;
    int resolve=1;
    PyObject *single = Py_False;
    if (!PyArg_ParseTuple(args, "|siO", &documentName,&resolve,&single))     // convert args: Python->C 
        return NULL;                             // NULL triggers exception

    std::vector<SelectionObject> sel;
    sel = Selection().getSelectionEx(documentName,
            App::DocumentObject::getClassTypeId(),resolve,PyObject_IsTrue(single));

    try {
        Py::List list;
        for (std::vector<SelectionObject>::iterator it = sel.begin(); it != sel.end(); ++it) {
            list.append(Py::asObject(it->getPyObject()));
        }
        return Py::new_reference_to(list);
    }
    catch (Py::Exception&) {
        return 0;
    }
}

PyObject *SelectionSingleton::sGetPickedList(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    char *documentName=0;
    if (!PyArg_ParseTuple(args, "|s", &documentName))     // convert args: Python->C 
        return NULL;                             // NULL triggers exception

    std::vector<SelectionObject> sel;
    sel = Selection().getPickedListEx(documentName);

    try {
        Py::List list;
        for (std::vector<SelectionObject>::iterator it = sel.begin(); it != sel.end(); ++it) {
            list.append(Py::asObject(it->getPyObject()));
        }
        return Py::new_reference_to(list);
    }
    catch (Py::Exception&) {
        return 0;
    }
}

PyObject *SelectionSingleton::sGetSelectionObject(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    char *docName, *objName, *subName;
    PyObject* tuple=0;
    if (!PyArg_ParseTuple(args, "sss|O!", &docName, &objName, &subName,
                                          &PyTuple_Type, &tuple))
        return NULL;

    try {
        SelectionObject selObj;
        selObj.DocName  = docName;
        selObj.FeatName = objName;
        std::string sub = subName;
        if (!sub.empty()) {
            selObj.SubNames.push_back(sub);
            if (tuple) {
                Py::Tuple t(tuple);
                double x = (double)Py::Float(t.getItem(0));
                double y = (double)Py::Float(t.getItem(1));
                double z = (double)Py::Float(t.getItem(2));
                selObj.SelPoses.push_back(Base::Vector3d(x,y,z));
            }
        }

        return selObj.getPyObject();
    }
    catch (const Py::Exception&) {
        return 0;
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(Base::BaseExceptionFreeCADError, e.what());
        return 0;
    }
}

PyObject *SelectionSingleton::sAddSelObserver(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    PyObject* o;
    int resolve = 1;
    if (!PyArg_ParseTuple(args, "O|i",&o,&resolve))
        return NULL;
    PY_TRY {
        SelectionObserverPython::addObserver(Py::Object(o),resolve);
        Py_Return;
    } PY_CATCH;
}

PyObject *SelectionSingleton::sRemSelObserver(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    PyObject* o;
    if (!PyArg_ParseTuple(args, "O",&o))
        return NULL;
    PY_TRY {
        SelectionObserverPython::removeObserver(Py::Object(o));
        Py_Return;
    } PY_CATCH;
}

PyObject *SelectionSingleton::sAddSelectionGate(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    char* filter;
    int resolve = 1;
    if (PyArg_ParseTuple(args, "s|i",&filter,&resolve)) {
        PY_TRY {
            Selection().addSelectionGate(new SelectionFilterGate(filter),resolve);
            Py_Return;
        } PY_CATCH;
    }

    PyErr_Clear();
    PyObject* filterPy;
    if (PyArg_ParseTuple(args, "O!|i",SelectionFilterPy::type_object(),&filterPy,resolve)) {
        PY_TRY {
            Selection().addSelectionGate(new SelectionFilterGatePython(
                        static_cast<SelectionFilterPy*>(filterPy)),resolve);
            Py_Return;
        } PY_CATCH;
    }

    PyErr_Clear();
    PyObject* gate;
    if (PyArg_ParseTuple(args, "O|i",&gate,&resolve)) {
        PY_TRY {
            Selection().addSelectionGate(new SelectionGatePython(Py::Object(gate, false)),resolve);
            Py_Return;
        } PY_CATCH;
    }

    PyErr_SetString(PyExc_ValueError, "Argument is neither string nor SelectionFiler nor SelectionGate");
    return 0;
}

PyObject *SelectionSingleton::sRemoveSelectionGate(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    PY_TRY {
        Selection().rmvSelectionGate();
    } PY_CATCH;

    Py_Return;
}

PyObject *SelectionSingleton::sSetVisible(PyObject * /*self*/, PyObject *args, PyObject * /*kwd*/)
{
    PyObject *visible = Py_None;
    if (!PyArg_ParseTuple(args, "|O",&visible))
        return NULL;                             // NULL triggers exception 

    PY_TRY {
        int vis;
        if(visible == Py_None)
            vis = -1;
        else 
            vis = PyObject_IsTrue(visible)?1:0;
        Selection().setVisible(vis);
    } PY_CATCH;

    Py_Return;
}


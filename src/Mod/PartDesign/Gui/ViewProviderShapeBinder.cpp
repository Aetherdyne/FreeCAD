/***************************************************************************
 *   Copyright (c) 2015 Stefan Tröger <stefantroeger@gmx.net>              *
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
# include <QApplication>
# include <QMessageBox>
# include <QMenu>
# include <QPainter>
# include <QMouseEvent>
# include <Inventor/nodes/SoSeparator.h>
# include <TopExp.hxx>
# include <TopTools_IndexedMapOfShape.hxx>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <set>
#include <unordered_set>

#include <Base/Console.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/ViewParams.h>
#include <Gui/BitmapFactory.h>

#include <Mod/PartDesign/App/ShapeBinder.h>

#include "ViewProvider.h"
#include "ViewProviderShapeBinder.h"
#include "TaskShapeBinder.h"

FC_LOG_LEVEL_INIT("ShapeBinder",true,true)

using namespace PartDesignGui;

PROPERTY_SOURCE(PartDesignGui::ViewProviderShapeBinder,PartGui::ViewProviderPart)

ViewProviderShapeBinder::ViewProviderShapeBinder()
{
    sPixmap = "PartDesign_ShapeBinder.svg";

    //make the viewprovider more datum like
    AngularDeflection.setStatus(App::Property::Hidden, true);
    Deviation.setStatus(App::Property::Hidden, true);
    DrawStyle.setStatus(App::Property::Hidden, true);
    Lighting.setStatus(App::Property::Hidden, true);
    LineColor.setStatus(App::Property::Hidden, true);
    LineWidth.setStatus(App::Property::Hidden, true);
    PointColor.setStatus(App::Property::Hidden, true);
    PointSize.setStatus(App::Property::Hidden, true);
    DisplayMode.setStatus(App::Property::Hidden, true);

    //get the datum coloring scheme
    // set default color for datums (golden yellow with 60% transparency)
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath (
            "User parameter:BaseApp/Preferences/Mod/PartDesign");
    unsigned long shcol = hGrp->GetUnsigned ( "DefaultDatumColor", 0xFFD70099 );
    App::Color col ( (uint32_t) shcol );
    
    MapFaceColor.setValue(false);
    MapLineColor.setValue(false);
    MapPointColor.setValue(false);
    MapTransparency.setValue(false);

    ShapeColor.setValue(col);
    LineColor.setValue(col);
    PointColor.setValue(col);
    Transparency.setValue(60);
    LineWidth.setValue(1);
}

ViewProviderShapeBinder::~ViewProviderShapeBinder()
{

}

bool ViewProviderShapeBinder::setEdit(int ModNum) {
    // TODO Share code with other view providers (2015-09-11, Fat-Zer)

    if (ModNum == ViewProvider::Default || ModNum == 1) {
        // When double-clicking on the item for this pad the
        // object unsets and sets its edit mode without closing
        // the task panel
        Gui::TaskView::TaskDialog *dlg = Gui::Control().activeDialog();
        TaskDlgShapeBinder *sbDlg = qobject_cast<TaskDlgShapeBinder*>(dlg);
        if (dlg && !sbDlg) {
            QMessageBox msgBox;
            msgBox.setText(QObject::tr("A dialog is already open in the task panel"));
            msgBox.setInformativeText(QObject::tr("Do you want to close this dialog?"));
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            msgBox.setDefaultButton(QMessageBox::Yes);
            int ret = msgBox.exec();
            if (ret == QMessageBox::Yes)
                Gui::Control().reject();
            else
                return false;
        }

        // clear the selection (convenience)
        Gui::Selection().clearSelection();

        // start the edit dialog
        // another pad left open its task panel
        if (sbDlg)
            Gui::Control().showDialog(sbDlg);
        else
            Gui::Control().showDialog(new TaskDlgShapeBinder(this,ModNum == 1));

        return true;
    }
    else {
        return ViewProviderPart::setEdit(ModNum);
    }
}

void ViewProviderShapeBinder::unsetEdit(int ModNum) {

    PartGui::ViewProviderPart::unsetEdit(ModNum);
}

void ViewProviderShapeBinder::highlightReferences(const bool on, bool /*auxiliary*/)
{
    App::GeoFeature* obj = nullptr;
    std::vector<std::string> subs;

    if (getObject()->isDerivedFrom(PartDesign::ShapeBinder::getClassTypeId()))
        PartDesign::ShapeBinder::getFilteredReferences(&static_cast<PartDesign::ShapeBinder*>(getObject())->Support, obj, subs);
    else
        return;

    // stop if not a Part feature was found
    if (!obj || !obj->getTypeId().isDerivedFrom(Part::Feature::getClassTypeId()))
        return;

    PartGui::ViewProviderPart* svp = dynamic_cast<PartGui::ViewProviderPart*>(
                Gui::Application::Instance->getViewProvider(obj));
    if (svp == NULL) return;

    if (on) {
         if (!subs.empty() && originalLineColors.empty()) {
            TopTools_IndexedMapOfShape eMap;
            TopExp::MapShapes(static_cast<Part::Feature*>(obj)->Shape.getValue(), TopAbs_EDGE, eMap);
            originalLineColors = svp->LineColorArray.getValues();
            std::vector<App::Color> lcolors = originalLineColors;
            lcolors.resize(eMap.Extent(), svp->LineColor.getValue());

            TopExp::MapShapes(static_cast<Part::Feature*>(obj)->Shape.getValue(), TopAbs_FACE, eMap);
            originalFaceColors = svp->DiffuseColor.getValues();
            std::vector<App::Color> fcolors = originalFaceColors;
            fcolors.resize(eMap.Extent(), svp->ShapeColor.getValue());

            for (std::string e : subs) {
                // Note: stoi may throw, but it strictly shouldn't happen
                if(e.substr(4) == "Edge") {
                    int idx = std::stoi(e.substr(4)) - 1;
                    assert ( idx>=0 );
                    if ( idx < (ssize_t) lcolors.size() )
                        lcolors[idx] = App::Color(1.0,0.0,1.0); // magenta
                }
                else if(e.substr(4) == "Face")  {
                    int idx = std::stoi(e.substr(4)) - 1;
                    assert ( idx>=0 );
                    if ( idx < (ssize_t) fcolors.size() )
                        fcolors[idx] = App::Color(1.0,0.0,1.0); // magenta
                }
            }
            svp->LineColorArray.setValues(lcolors);
            svp->DiffuseColor.setValues(fcolors);
        }
    } else {
        if (!subs.empty() && !originalLineColors.empty()) {
            svp->LineColorArray.setValues(originalLineColors);
            originalLineColors.clear();

            svp->DiffuseColor.setValues(originalFaceColors);
            originalFaceColors.clear();
        }
    }
}

void ViewProviderShapeBinder::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    QAction* act;
    act = menu->addAction(QObject::tr("Edit shape binder"), receiver, member);
    act->setData(QVariant((int)ViewProvider::Default));
}

//=====================================================================================

PROPERTY_SOURCE(PartDesignGui::ViewProviderSubShapeBinder,PartGui::ViewProviderPart)

ViewProviderSubShapeBinder::ViewProviderSubShapeBinder() {
    sPixmap = "PartDesign_SubShapeBinder.svg";

    ADD_PROPERTY_TYPE(UseBinderStyle, (false), "",(App::PropertyType)(App::Prop_None), "");
    ForceMapColors.setValue(true);
}

void ViewProviderSubShapeBinder::attach(App::DocumentObject *obj) {

    UseBinderStyle.setValue(boost::istarts_with(obj->getNameInDocument(),"binder"));
    ViewProviderPart::attach(obj);
}

template<class P, class V>
void setProperty(P &prop, const V &v) {
    Base::ObjectStatusLocker<App::Property::Status, App::Property> lock(App::Property::User3, &prop);
    prop.setValue(v);
}

void ViewProviderSubShapeBinder::onChanged(const App::Property *prop) {
    if(prop == &UseBinderStyle
            && (!getObject() || !getObject()->isRestoring()))
    {
        App::Color shapeColor,lineColor,pointColor;
        int transparency, linewidth;
        bool mapFace,mapLine,mapPoint,mapTrans;
        if(UseBinderStyle.getValue()) {
            //get the datum coloring scheme
            // set default color for datums (golden yellow with 60% transparency)
            static ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath (
                    "User parameter:BaseApp/Preferences/Mod/PartDesign");
            shapeColor.setPackedValue(hGrp->GetUnsigned ( "DefaultDatumColor", 0xFFD70099 ));
            lineColor = shapeColor;
            pointColor = shapeColor;
            transparency = 60;
            linewidth = 1;
            mapFace = mapLine = mapPoint = mapTrans = false;
        } else {
            shapeColor.setPackedValue(Gui::ViewParams::instance()->getDefaultShapeColor());
            lineColor.setPackedValue(Gui::ViewParams::instance()->getDefaultShapeLineColor());
            pointColor = lineColor;
            transparency = 0;
            linewidth = Gui::ViewParams::instance()->getDefaultShapeLineWidth();
#if 0
            static ParameterGrp::handle hPart = App::GetApplication().GetParameterGroupByPath
                ("User parameter:BaseApp/Preferences/Mod/Part");
            mapFace = hPart->GetBool("MapFaceColor");
            mapLine = hPart->GetBool("MapLineColor");
            mapPoint = hPart->GetBool("MapPointColor");
            mapTrans = hPart->GetBool("MapTransparency");
#else
            mapFace = true;
            mapLine = true;
            mapPoint = true;
            mapTrans = true;
#endif
        }

        setProperty(LineColor, lineColor);
        setProperty(PointColor, pointColor);
        setProperty(ShapeColor, shapeColor);
        setProperty(Transparency, transparency);
        setProperty(LineWidth, linewidth);
        setProperty(MapFaceColor, mapFace);
        setProperty(MapLineColor, mapLine);
        setProperty(MapPointColor, mapPoint);
        setProperty(MapTransparency, mapTrans);
        updateColors();
    }
    else if (prop == &ShapeColor) {
        if (!prop->testStatus(App::Property::User3))
            setProperty(MapFaceColor, false);
    }
    else if (prop == &LineColor) {
        if (!prop->testStatus(App::Property::User3))
            setProperty(MapLineColor, false);
    }
    else if (prop == &PointColor) {
        if (!prop->testStatus(App::Property::User3))
            setProperty(MapPointColor, false);
    }
    else if (prop == &Transparency) {
        if (!prop->testStatus(App::Property::User3))
            setProperty(MapTransparency, false);
    }

    ViewProviderPart::onChanged(prop);
}

void ViewProviderSubShapeBinder::updateData(const App::Property *prop)
{
    auto binder = Base::freecad_dynamic_cast<PartDesign::SubShapeBinder>(getObject());
    if (binder) {
        if (prop == &binder->Support) {
            iconChangeConns.clear();
            std::set<App::SubObjectT> objs;
            for(auto &l : binder->Support.getSubListValues()) {
                for (auto & sub : l.getSubValues()) {
                    auto res = objs.emplace(l.getValue(), sub.c_str());
                    if (!res.second)
                        continue;
                    auto vp = Gui::Application::Instance->getViewProvider(res.first->getSubObject());
                    if (vp) {
                        iconChangeConns.push_back(vp->signalChangeIcon.connect(
                            [this]() { this->iconMap.clear(); this->signalChangeIcon(); }));
                    }
                }
            }
            this->iconMap.clear();
            signalChangeIcon();
        }
    }
    ViewProviderPart::updateData(prop);
}

bool ViewProviderSubShapeBinder::canDropObjectEx(App::DocumentObject *, 
        App::DocumentObject *, const char *, const std::vector<std::string> &) const
{
    return true;
}

std::string ViewProviderSubShapeBinder::dropObjectEx(App::DocumentObject *obj, App::DocumentObject *owner,
        const char *subname, const std::vector<std::string> &elements)
{
    auto self = dynamic_cast<PartDesign::SubShapeBinder*>(getObject());
    if(!self) return std::string(".");
    std::map<App::DocumentObject *, std::vector<std::string> > values;
    if(!subname) subname = "";
    std::string sub(subname);
    if(sub.empty()) 
        values[owner?owner:obj] = elements;
    else {
        std::vector<std::string> subs;
        if(elements.size()) {
            subs.reserve(elements.size());
            for(auto &element : elements)
                subs.push_back(sub+element);
        }else
            subs.push_back(sub);
        values[owner?owner:obj] = std::move(subs);
    }

    int dropid = Gui::isTreeViewDropping();
    self->setLinks(std::move(values),
            QApplication::keyboardModifiers()==Qt::ControlModifier && _dropID != dropid);
    _dropID = dropid;
    if(self->Relative.getValue())
        updatePlacement(false);
    return std::string(".");
}

bool ViewProviderSubShapeBinder::doubleClicked() {
    updatePlacement(true);
    return true;
}

void ViewProviderSubShapeBinder::setupContextMenu(QMenu* menu, QObject* receiver, const char* member) 
{
    QAction* act;
    act = menu->addAction(QObject::tr("Synchronize"), receiver, member);
    act->setData(QVariant((int)0));
    act = menu->addAction(QObject::tr("Select bound object"), receiver, member);
    act->setData(QVariant((int)0x81));
    ViewProviderPart::setupContextMenu(menu,receiver,member);
}

bool ViewProviderSubShapeBinder::setEdit(int ModNum) {
    
    switch(ModNum) {
    case 0:
        updatePlacement(true);
        break;
    case 0x81: {
        auto self = dynamic_cast<PartDesign::SubShapeBinder*>(getObject());
        if(!self || !self->Support.getValue())
            break;

        Gui::Selection().selStackPush();
        Gui::Selection().clearSelection();
        for(auto &link : self->Support.getSubListValues()) {
            auto obj = link.getValue();
            if(!obj || !obj->getNameInDocument())
                continue;
            const auto &subs = link.getSubValues();
            if(subs.size())
                Gui::Selection().addSelections(obj->getDocument()->getName(),
                        obj->getNameInDocument(),subs);
            else
                Gui::Selection().addSelection(obj->getDocument()->getName(),
                        obj->getNameInDocument());
        }
        Gui::Selection().selStackPush();
        break;
    }
    default:
        return ViewProviderPart::setEdit(ModNum);
    }
    return false;
}

void ViewProviderSubShapeBinder::updatePlacement(bool transaction) {
    auto self = dynamic_cast<PartDesign::SubShapeBinder*>(getObject());
    if(!self || !self->Support.getValue())
        return;

    std::vector<Base::Matrix4D> mats;
    bool relative = self->Relative.getValue();
    App::DocumentObject *parent = 0;
    std::string parentSub;
    if(relative && self->getParents().size()) {
        const auto &sel = Gui::Selection().getSelection("",0);
        if(sel.size()!=1 || !sel[0].pObject ||
            sel[0].pObject->getSubObject(sel[0].SubName)!=self) 
        {
            FC_WARN("invalid selection");
        } else {
            parent = sel[0].pObject;
            parentSub = sel[0].SubName;
        }
    }

    if(!transaction) {
        if(relative)
            self->Context.setValue(parent,parentSub.c_str());
        self->update(PartDesign::SubShapeBinder::UpdateForced);
        return;
    }

    App::GetApplication().setActiveTransaction("Sync binder");
    try{
        if(relative)
            self->Context.setValue(parent,parentSub.c_str());
        self->update(PartDesign::SubShapeBinder::UpdateForced);
        App::GetApplication().closeActiveTransaction();
        return;
    }catch(Base::Exception &e) {
        e.ReportException();
    }catch(Standard_Failure &e) {
        std::ostringstream str;
        Standard_CString msg = e.GetMessageString();
        str << typeid(e).name() << " ";
        if (msg) {str << msg;}
        else     {str << "No OCCT Exception Message";}
        FC_ERR(str.str());
    }
    App::GetApplication().closeActiveTransaction(true);
}

std::vector<App::DocumentObject*> ViewProviderSubShapeBinder::claimChildren(void) const {
    std::vector<App::DocumentObject *> ret;
    auto self = Base::freecad_dynamic_cast<PartDesign::SubShapeBinder>(getObject());
    if(self && self->ClaimChildren.getValue() && self->Support.getValue()) {
        std::set<App::DocumentObject *> objSet;
        for(auto &l : self->Support.getSubListValues()) {
            auto obj = l.getValue();
            if(!obj)
                continue;
            const auto &subs = l.getSubValues();
            if(subs.empty()) {
                if(objSet.insert(obj).second)
                    ret.push_back(obj);
                continue;
            }
            for(auto &sub : subs) {
                auto sobj = obj->getSubObject(sub.c_str());
                if(sobj && objSet.insert(sobj).second)
                    ret.push_back(sobj);
            }
        }
    }
    return ret;
}

struct PixmapInfo {
    int count = 0;
    QByteArray tag;

    void generateIcon(QPixmap &px, const QPixmap &pxTag)
    {
        if (++this->count > 2)
            return;

        int tagWidth = pxTag.isNull() ? 0 : pxTag.width()/2;

        if (this->count == 1) {
            QPixmap pxOrig = px;
            px = QPixmap(64 + tagWidth, 64);
            px.fill(Qt::transparent);
            QPainter pt;
            pt.begin(&px);
            pt.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
            pt.setPen(Qt::NoPen);
            pt.setBrush(Qt::white);
            pt.drawRect(QRect(5, 5, 52, 52));
            pt.drawPixmap(7, 7, 48, 48, pxOrig, 0, 0, pxOrig.width(), pxOrig.height());
            pt.setPen(QPen(Qt::black, 2));
            pt.setBrush(QBrush());
            pt.drawRect(QRect(5, 5, 52, 52));
            if (!pxTag.isNull())
                pt.drawPixmap(64-tagWidth, 0, pxTag);
            pt.end();
            return;
        }

        QPixmap pxCopy = px;
        px = QPixmap(pxCopy.size());
        px.fill(Qt::transparent);
        QPainter pt;
        pt.begin(&px);
        pt.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
        pt.setPen(QPen(Qt::black, 2));
        pt.setBrush(Qt::white);
        pt.drawRect(QRect(1, 1, 53, 53));
        pt.drawPixmap(5, 5, pxCopy);
        pt.end();
    }
};

Gui::ViewProviderDocumentObject *ViewProviderSubShapeBinder::getLinkedViewProvider(
        std::string *subname, bool recursive) const
{
    (void)subname;
    auto self = const_cast<ViewProviderSubShapeBinder*>(this);
    if(!pcObject || !pcObject->getNameInDocument())
        return self;

    auto binder = Base::freecad_dynamic_cast<PartDesign::SubShapeBinder>(pcObject);
    auto linked = binder ? binder->_getLinkedObject(recursive) : pcObject->getLinkedObject(recursive);
    if(!linked || linked == pcObject)
        return self;
    auto res = Base::freecad_dynamic_cast<Gui::ViewProviderDocumentObject>(
            Gui::Application::Instance->getViewProvider(linked));
    if(!res)
        res = self;
    return res;
}

void ViewProviderSubShapeBinder::getExtraIcons(
        std::vector<std::pair<QByteArray, QPixmap> > &icons) const
{
    generateIcons();
    for (auto &v : iconMap)
        icons.emplace_back(v.first, v.second.pixmap);
}

QString ViewProviderSubShapeBinder::getToolTip(const QByteArray &tag) const
{
    generateIcons();

    std::ostringstream ss;
    auto doc = getObject()->getDocument()->getName();

    if (tag == Gui::TreeWidget::getMainIconTag()) {
        auto self = dynamic_cast<PartDesign::SubShapeBinder*>(getObject());
        if (!self)
            return QString();
        for(auto &link : self->Support.getSubListValues()) {
            auto obj = link.getValue();
            if(!obj || !obj->getNameInDocument())
                continue;
            const auto &subs = link.getSubValues();
            if(subs.size()) {
                for (auto &sub : subs)
                    ss << "\n" << App::SubObjectT(obj, sub.c_str()).getSubObjectFullName(doc);
            } else
                ss << "\n" << App::SubObjectT(obj, "").getObjectFullName(doc);
        }
    } else {
        auto it = iconMap.find(tag);
        if (it == iconMap.end())
            return inherited::getToolTip(tag);
        for (auto &objT : it->second.objs)
            ss << "\n" << objT.getSubObjectFullName(doc);
    }

    if (!ss.tellp())
        return QString();
    return QString::fromLatin1("%1%2").arg(
            QObject::tr("Bound objects. ALT + click this icon to select.\n"),
            QString::fromUtf8(ss.str().c_str()));
}

bool ViewProviderSubShapeBinder::iconMouseEvent(QMouseEvent *ev, const QByteArray &tag)
{
    auto self = dynamic_cast<PartDesign::SubShapeBinder*>(getObject());
    if (!self)
        return false;
    if (ev->type() == QEvent::MouseButtonPress) {
        const std::vector<App::SubObjectT> *objs = nullptr;
        std::vector<App::SubObjectT> _objs;
        if (tag == Gui::TreeWidget::getMainIconTag()) {
            for(auto &link : self->Support.getSubListValues()) {
                auto obj = link.getValue();
                if(!obj || !obj->getNameInDocument())
                    continue;
                const auto &subs = link.getSubValues(false);
                if(subs.size()) {
                    for (auto &sub : subs)
                        _objs.emplace_back(obj, sub.c_str());
                } else
                    _objs.emplace_back(obj, "");
            }
            objs = &_objs;
        } else {
            auto it = iconMap.find(tag);
            if (it == iconMap.end())
                return inherited::iconMouseEvent(ev, tag);
            objs = &it->second.objs;
        }
        if (!objs || objs->empty())
            return false;

        bool singleSelect = !(ev->modifiers() & Qt::ControlModifier);
        if (singleSelect) {
            Gui::Selection().selStackPush();
            Gui::Selection().clearSelection();
        }
        for (auto &objT : *objs)
            Gui::Selection().addSelection(objT);
        if (singleSelect)
            Gui::Selection().selStackPush();
        return true;
    }

    return inherited::iconMouseEvent(ev, tag);
}

void ViewProviderSubShapeBinder::generateIcons() const
{
    if (iconMap.size())
        return;

    auto binder = Base::freecad_dynamic_cast<PartDesign::SubShapeBinder>(getObject());
    if (!binder)
        return;

    static QPixmap myPixmap;
    if (myPixmap.isNull())
       myPixmap = Gui::BitmapFactory().pixmap(this->sPixmap).scaled(
               32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    std::unordered_map<qint64, PixmapInfo> cacheKeys;
    for(auto &l : binder->Support.getSubListValues()) {
        for (auto & sub : l.getSubValues(false)) {
            App::SubObjectT sobjT(l.getValue(), sub.c_str());
            auto sobj = sobjT.getSubObject();
            if (!sobj)
                continue;

            auto binder = Base::freecad_dynamic_cast<PartDesign::SubShapeBinder>(sobj);
            Gui::ViewProvider *vp = nullptr;
            QPixmap px;
            if (binder) {
                // binder of binder, extract its first bound object's icon
                vp = Gui::Application::Instance->getViewProvider(
                        binder->_getLinkedObject(true));
            } else
                vp = Gui::Application::Instance->getViewProvider(sobj);

            if (vp) {
                px = vp->getIcon().pixmap(64, 64);
                unsigned long tag = 0;
                auto featVp = Base::freecad_dynamic_cast<PartDesignGui::ViewProvider>(vp);
                if (featVp)
                    tag = featVp->IconColor.getValue().getPackedValue();

                auto & pxInfo = cacheKeys[(binder?myPixmap.cacheKey():0) ^ px.cacheKey() ^ tag];
                QPixmap pxTag;
                auto it = iconMap.begin();
                if (pxInfo.count == 0) {
                    if (iconMap.size() >= 3)
                        break;
                    if (binder)
                        px = Gui::BitmapFactory().merge(px, myPixmap, Gui::BitmapFactoryInst::TopLeft);
                    if (tag)
                        pxTag = featVp->getTagIcon().scaled(40, 40,
                            Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    pxInfo.tag = QByteArray("Binder:") + QByteArray::number((int)iconMap.size());
                    it = iconMap.emplace(pxInfo.tag, IconInfo()).first;
                    it->second.pixmap = px;
                } else {
                    it = iconMap.find(pxInfo.tag);
                    assert(it != iconMap.end());
                }
                pxInfo.generateIcon(it->second.pixmap, pxTag);
                it->second.objs.push_back(std::move(sobjT));
            }
        }
        if (iconMap.size() >= 3)
            break;
    }
}

////////////////////////////////////////////////////////////////////////////////////////

namespace Gui {
PROPERTY_SOURCE_TEMPLATE(PartDesignGui::ViewProviderSubShapeBinderPython,
                         PartDesignGui::ViewProviderSubShapeBinder)
template class PartDesignGuiExport ViewProviderPythonFeatureT<ViewProviderSubShapeBinder>;
}

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

#ifndef _PreComp_
# include <sstream>
# include <gp_Trsf.hxx>
# include <gp_Ax1.hxx>
# include <BRepBuilderAPI_MakeShape.hxx>
# include <BRepAlgoAPI_Fuse.hxx>
# include <BRepAlgoAPI_Common.hxx>
# include <TopTools_ListIteratorOfListOfShape.hxx>
# include <TopExp.hxx>
# include <TopExp_Explorer.hxx>
# include <TopTools_IndexedMapOfShape.hxx>
# include <Standard_Failure.hxx>
# include <Standard_Version.hxx>
# include <TopoDS_Face.hxx>
# include <gp_Dir.hxx>
# include <gp_Pln.hxx> // for Precision::Confusion()
# include <Bnd_Box.hxx>
# include <BRepBndLib.hxx>
# include <BRepExtrema_DistShapeShape.hxx>
# include <BRepAdaptor_Curve.hxx>
# include <TopoDS.hxx>
# include <GProp_GProps.hxx>
# include <BRepGProp.hxx>
# include <gce_MakeLin.hxx>
# include <BRepIntCurveSurface_Inter.hxx>
# include <IntCurveSurface_IntersectionPoint.hxx>
# include <gce_MakeDir.hxx>
# include <BRepBuilderAPI_MakeEdge.hxx>
# include <BRepBuilderAPI_MakeFace.hxx>
# include <BRepBuilderAPI_MakeVertex.hxx>
# include <BRepBuilderAPI_Copy.hxx>
#endif

#include <boost/range.hpp>
typedef boost::iterator_range<const char*> CharRange;

#include <boost/algorithm/string/predicate.hpp>
#include <boost_bind_bind.hpp>
#include <Base/Console.h>
#include <Base/Writer.h>
#include <Base/Reader.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>
#include <Base/Placement.h>
#include <Base/Rotation.h>
#include <Base/Tools.h>
#include <App/Application.h>
#include <App/FeaturePythonPyImp.h>
#include <App/Document.h>
#include <App/Link.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/MappedElement.h>
#include <App/OriginFeature.h>
#include <App/Placement.h>

#include "PartPyCXX.h"
#include "PartFeature.h"
#include "PartFeaturePy.h"
#include "TopoShapePy.h"
#include "PartParams.h"
#include "TopoShapeOpCode.h"

using namespace Part;
namespace bp = boost::placeholders;

FC_LOG_LEVEL_INIT("Part",true,true)

PROPERTY_SOURCE(Part::Feature, App::GeoFeature)


Feature::Feature(void)
{
    ADD_PROPERTY(Shape, (TopoDS_Shape()));
    ADD_PROPERTY_TYPE(ValidateShape, (false), "", App::Prop_None,
            "Validate shape content and warn about invalid shape");
    ADD_PROPERTY_TYPE(InvalidShape, (false), "", App::Prop_Hidden,
            "Indicate the shape is invalid");
    ADD_PROPERTY_TYPE(FixShape, (PartParams::getFixShape()?1l:0l), "", App::Prop_None,
            "Fix shape content.\n"
            "Disabled: no fix.\n"
            "Enabled: validate shape and only fix invalid one.\n"
            "Always: always try to fix shape without validating first.\n");
    static const char *FixShapeEnum[] = {"Disabled", "Enabled", "Always", nullptr};
    FixShape.setEnums(FixShapeEnum);
    ADD_PROPERTY_TYPE(ColoredElements, (0), "",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_ReadOnly|App::Prop_Output),"");
}

Feature::~Feature()
{
}

void Feature::fixShape(TopoShape &s) const
{
    if (FixShape.getValue()) {
        if (FixShape.getValue() == 2 || !s.isValid())
            s.fix();
    }
}

short Feature::mustExecute(void) const
{
    return GeoFeature::mustExecute();
}

App::DocumentObjectExecReturn *Feature::recompute(void)
{
    try {
        return App::GeoFeature::recompute();
    }
    catch (Standard_Failure& e) {

        App::DocumentObjectExecReturn* ret = new App::DocumentObjectExecReturn(e.GetMessageString());
        if (ret->Why.empty()) ret->Why = "Unknown OCC exception";
        return ret;
    }
}

App::DocumentObjectExecReturn *Feature::execute(void)
{
    mergeShapeContents();
    return GeoFeature::execute();
}

PyObject *Feature::getPyObject(void)
{
    if (PythonObject.is(Py::_None())){
        // ref counter is set to 1
        PythonObject = Py::Object(new PartFeaturePy(this),true);
    }
    return Py::new_reference_to(PythonObject);
}

std::pair<std::string,std::string>
Feature::getElementName(const char *name, ElementNameType type) const
{
    if (type != ElementNameType::Export)
        return App::GeoFeature::getElementName(name, type);

    // This function is overridden to provide higher level shape topo names that
    // are generated on demand, e.g. Wire, Shell, Solid, etc.

    auto prop = Base::freecad_dynamic_cast<PropertyPartShape>(getPropertyOfGeometry());
    if (!prop)
        return App::GeoFeature::getElementName(name, type);

    return getExportElementName(prop->getShape(), name);
}

std::pair<std::string,std::string>
Feature::getExportElementName(TopoShape shape, const char *name) const
{
    Data::MappedElement mapped = shape.getElementName(name);
    auto res = shape.shapeTypeAndIndex(mapped.index);
    static const int MinLowerTopoNames = 3;
    static const int MaxLowerTopoNames = 10;
    if (res.second && !mapped.name) {
        // Here means valid index name, but no mapped name, check to see if
        // we shall generate the high level topo name.
        //
        // The general idea of the algorithm is to find the minimum number of
        // lower elements that can identify the given higher element, and
        // combine their names to generate the name for the higher element.
        //
        // In theory, all it takes to find one lower element that only appear
        // in the given higher element. To make the algorithm more robust
        // against model changes, we shall take minimum MinLowerTopoNames lower
        // elements.
        //
        // On the other hand, it may be possible to take too many elements for
        // disambiguation. We shall limit to maximum MaxLowerTopoNames. If the
        // chosen elements are not enough to disambiguate the higher element,
        // we'll include an index for disambiguation.

        auto subshape = shape.getSubTopoShape(res.first, res.second, true);
        TopAbs_ShapeEnum lower;
        Data::IndexedName idxName;
        if (!subshape.isNull()) {
            switch(res.first) {
            case TopAbs_WIRE:
                lower = TopAbs_EDGE;
                idxName = Data::IndexedName::fromConst("Edge", 1);
                break;
            case TopAbs_SHELL:
            case TopAbs_SOLID:
            case TopAbs_COMPOUND:
            case TopAbs_COMPSOLID:
                lower = TopAbs_FACE;
                idxName = Data::IndexedName::fromConst("Face", 1);
                break;
            default:
                lower = TopAbs_SHAPE;
            }
            if (lower != TopAbs_SHAPE) {
                typedef std::pair<size_t, std::vector<int>> NameEntry;
                std::vector<NameEntry> indices;
                std::vector<Data::MappedName> names;
                std::vector<int> ancestors;
                int count = 0;
                for (auto &ss : subshape.getSubTopoShapes(lower)) {
                    auto name = ss.getMappedName(idxName);
                    if (!name) continue;
                    indices.emplace_back(name.size(), shape.findAncestors(ss.getShape(), res.first));
                    names.push_back(name);
                    if (indices.back().second.size() == 1 && ++count >= MinLowerTopoNames)
                        break;
                }

                if (names.size() >= MaxLowerTopoNames) {
                    std::stable_sort(indices.begin(), indices.end(),
                        [](const NameEntry &a, const NameEntry &b) {
                            return a.second.size() < b.second.size();
                        });
                    std::vector<Data::MappedName> sorted;
                    auto pos = 0;
                    sorted.reserve(names.size());
                    for (auto &v : indices) {
                        size_t size = ancestors.size();
                        if (size == 0)
                            ancestors = v.second;
                        else if (size > 1) {
                            for (auto it = ancestors.begin(); it != ancestors.end(); ) {
                                if (std::find(v.second.begin(), v.second.end(), *it) == v.second.end()) {
                                    it = ancestors.erase(it);
                                    if (ancestors.size() == 1)
                                        break;
                                } else
                                    ++it;
                            }
                        }
                        auto itPos = sorted.end();
                        if (size == 1 || size != ancestors.size()) {
                            itPos = sorted.begin() + pos;
                            ++pos;
                        }
                        sorted.insert(itPos, names[v.first]);
                        if (size == 1 && sorted.size() >= MinLowerTopoNames)
                            break;
                    }
                }

                names.resize(std::min((int)names.size(), MaxLowerTopoNames));
                if (names.size()) {
                    std::string op;
                    if (ancestors.size() > 1) {
                        // The current chosen elements are not enough to
                        // identify the higher element, generate an index for
                        // disambiguation.
                        auto it = std::find(ancestors.begin(), ancestors.end(), res.second);
                        if (it == ancestors.end())
                            assert(0 && "ancestor not found"); // this shouldn't happen
                        else
                            op = Data::ComplexGeoData::indexPostfix() + std::to_string(it - ancestors.begin());
                    }

                    // Note: setting names to shape will change its underlying
                    // shared element name table. This actually violates the
                    // const'ness of this function.
                    //
                    // To be const correct, we should have made the element
                    // name table to be implicit sharing (i.e. copy on change).
                    //
                    // Not sure if there is any side effect of indirectly
                    // change the element map inside the Shape property without
                    // recording the change in undo stack.
                    //
                    mapped.name = shape.setElementComboName(
                            mapped.index, names, mapped.index.getType(), op.c_str());
                }
            }
        }
    }
    else if (!res.second && mapped.name) {
        const char *dot = strchr(name,'.');
        if(dot) {
            ++dot;
            // Here means valid mapped name, but cannot find the corresponding
            // indexed name. This usually means the model has been changed. The
            // original indexed name is usually appended to the mapped name
            // separated by a dot. We use it as a clue to decode the combo name
            // set above, and try to single out one sub shape that has all the
            // lower elements encoded in the combo name. But since we don't
            // always use all the lower elements for encoding, this can only be
            // consider a heuristics.
            if (Data::ComplexGeoData::hasMissingElement(dot))
                dot += Data::ComplexGeoData::missingPrefix().size();
            std::pair<TopAbs_ShapeEnum, int> occindex = shape.shapeTypeAndIndex(dot);
            if (occindex.second > 0) {
                auto idxName = Data::IndexedName::fromConst(
                        shape.shapeName(occindex.first).c_str(), occindex.second);
                std::string postfix;
                auto names = shape.decodeElementComboName(idxName, mapped.name, idxName.getType(), &postfix);
                std::vector<int> ancestors;
                for (auto &name : names) {
                    auto index = shape.getIndexedName(name);
                    if (!index) {
                        ancestors.clear();
                        break;
                    }
                    auto oidx = shape.shapeTypeAndIndex(index);
                    auto subshape = shape.getSubShape(oidx.first, oidx.second);
                    if (subshape.IsNull()) {
                        ancestors.clear();
                        break;
                    }
                    auto current = shape.findAncestors(subshape, occindex.first); 
                    if (ancestors.empty())
                        ancestors = std::move(current);
                    else {
                        for (auto it = ancestors.begin(); it != ancestors.end();) {
                            if (std::find(current.begin(), current.end(), *it) == current.end())
                                it = ancestors.erase(it);
                            else
                                ++it;
                        }
                        if (ancestors.empty()) // model changed beyond recognition, bail!
                            break;
                    }
                }
                if (ancestors.size() > 1
                        && boost::starts_with(postfix, Data::ComplexGeoData::indexPostfix())) {
                    std::istringstream iss(postfix.c_str() + Data::ComplexGeoData::indexPostfix().size());
                    int idx;
                    if (iss >> idx && idx >= 0 && idx < (int)ancestors.size())
                        ancestors.resize(1, ancestors[idx]);
                }
                if (ancestors.size() == 1) {
                    idxName.setIndex(ancestors.front());
                    mapped.index = idxName;
                }
            }
        }
    }
    return App::GeoFeature::_getElementName(name, mapped);
}

App::DocumentObject *Feature::getSubObject(const char *subname, 
        PyObject **pyObj, Base::Matrix4D *pmat, bool transform, int depth) const
{
    while(subname && *subname=='.') ++subname; // skip leading .

    // having '.' inside subname means it is referencing some children object,
    // instead of any sub-element from ourself
    if(subname && !Data::ComplexGeoData::isMappedElement(subname) && strchr(subname,'.')) 
        return App::DocumentObject::getSubObject(subname,pyObj,pmat,transform,depth);

    Base::Matrix4D _mat;
    auto &mat = pmat?*pmat:_mat;
    if(transform)
        mat *= Placement.getValue().toMatrix();

    if(!pyObj) {
#if 0
        if(subname==0 || *subname==0 || Shape.getShape().hasSubShape(subname))
            return const_cast<Feature*>(this);
        return nullptr;
#else
        // TopoShape::hasSubShape is kind of slow, let's cut outself some slack here.
        return const_cast<Feature*>(this);
#endif
    }

    try {
        TopoShape ts(Shape.getShape());
        bool doTransform = mat!=ts.getTransform();
        if(doTransform) 
            ts.setShape(ts.getShape().Located(TopLoc_Location()),false);
        if(subname && *subname && !ts.isNull())
            ts = ts.getSubTopoShape(subname,true);
        if(doTransform && !ts.isNull()) {
            bool copy = PartParams::getCopySubShape();
            if(!copy) {
                // Work around OCC bug on transforming circular edge with an
                // offset surface. The bug probably affect other shape type,
                // too.
                TopExp_Explorer exp(ts.getShape(),TopAbs_EDGE);
                if(exp.More()) {
                    auto edge = TopoDS::Edge(exp.Current());
                    exp.Next();
                    if(!exp.More()) {
                        BRepAdaptor_Curve curve(edge);
                        copy = curve.GetType() == GeomAbs_Circle;
                    }
                }
            }
            ts.transformShape(mat,copy,true);
        }
        *pyObj =  Py::new_reference_to(shape2pyshape(ts));
        return const_cast<Feature*>(this);
    }
    catch(Standard_Failure &e) {
        // FIXME: Do not handle the exception here because it leads to a flood of irrelevant and
        // annoying error messages.
        // For example: https://forum.freecadweb.org/viewtopic.php?f=19&t=42216
        // Instead either raise a sub-class of Base::Exception and let it handle by the calling
        // instance or do simply nothing. For now the error message is degraded to a log message.
        std::ostringstream str;
        Standard_CString msg = e.GetMessageString();
#if OCC_VERSION_HEX >= 0x070000
        // Avoid name mangling
        str << e.DynamicType()->get_type_name() << " ";
#else
        str << typeid(e).name() << " ";
#endif
        if (msg) {str << msg;}
        else     {str << "No OCCT Exception Message";}
        str << ": " << getFullName();
        if (subname) 
            str << '.' << subname;
        FC_LOG(str.str());
        return 0;
    }
}

static std::vector<std::pair<long,Data::MappedName> > 
getElementSource(App::DocumentObject *owner, 
        TopoShape shape, const Data::MappedName & name, char type)
{
    std::set<std::pair<App::Document*, long>> tagSet;
    std::vector<std::pair<long,Data::MappedName> > ret;
    ret.emplace_back(0,name);
    int depth = 0;
    while(1) {
        Data::MappedName original;
        std::vector<Data::MappedName> history;
        // It is possible the name does not belong to the shape, e.g. when user
        // changes modeling order in PartDesign. So we try to assign the
        // document hasher here in case getElementHistory() needs to de-hash
        if(!shape.Hasher && owner)
            shape.Hasher = owner->getDocument()->getStringHasher();
        long tag = shape.getElementHistory(ret.back().second,&original,&history);
        if(!tag) 
            break;
        auto obj = owner;
        App::Document *doc = nullptr;
        if(owner) {
            doc = owner->getDocument();
            for(;;++depth) {
                auto linked = owner->getLinkedObject(false,nullptr,false,depth);
                if (linked == owner)
                    break;
                owner = linked;
                if (owner->getDocument() != doc) {
                    doc = owner->getDocument();
                    break;
                }
            }
            if (owner->isDerivedFrom(App::GeoFeature::getClassTypeId())) {
                auto o = static_cast<App::GeoFeature*>(owner)->getElementOwner(ret.back().second);
                if (o)
                    doc = o->getDocument();
            }
            obj = doc->getObjectByID(tag < 0 ? -tag : tag);
            if(type) {
                for(auto &hist : history) {
                    if(shape.elementType(hist)!=type)
                        return ret;
                }
            }
        }
        owner = 0;
        if(!obj) {
            // Object maybe deleted, but it is still possible to extract the
            // source element name from hasher table.
            shape.setShape(TopoDS_Shape());
            doc = nullptr;
        }else 
            shape = Part::Feature::getTopoShape(obj,0,false,0,&owner); 
        if(type && shape.elementType(original)!=type)
            break;

        if (std::abs(tag) != ret.back().first
                && !tagSet.insert(std::make_pair(doc,tag)).second) {
            // Because an object might be deleted, which may be a link/binder
            // that points to an external object that contain element name
            // using external hash table. We shall prepare for circular element
            // map due to looking up in the wrong table.
            if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
                FC_WARN("circular element mapping");
            break;
        }
        ret.emplace_back(tag,original);
    }
    return ret;
}

std::list<Data::HistoryItem> 
Feature::getElementHistory(App::DocumentObject *feature, 
        const char *name, bool recursive, bool sameType)
{
    std::list<Data::HistoryItem> ret;
    TopoShape shape = getTopoShape(feature);
    Data::IndexedName idx(name);
    Data::MappedName element;
    Data::MappedName prevElement;
    if (idx)
        element = shape.getMappedName(idx, true);
    else if (Data::ComplexGeoData::isMappedElement(name))
        element = Data::MappedName(Data::ComplexGeoData::newElementName(name));
    else
        element = Data::MappedName(name);
    char element_type=0;
    if(sameType)
        element_type = shape.elementType(element);
    int depth = 0;
    do {
        Data::MappedName original;
        ret.emplace_back(feature,element);
        long tag = shape.getElementHistory(element,&original,&ret.back().intermediates);

        ret.back().index = shape.getIndexedName(element);
        if (!ret.back().index && prevElement) {
            ret.back().index = shape.getIndexedName(prevElement);
            if (ret.back().index) {
                ret.back().intermediates.insert(ret.back().intermediates.begin(), element);
                ret.back().element = prevElement;
            }
        }
        if (ret.back().intermediates.size())
            prevElement = ret.back().intermediates.back();
        else
            prevElement = Data::MappedName();

        App::DocumentObject *obj = nullptr;
        if (tag) {
            App::Document *doc = feature->getDocument();
            for(;;++depth) {
                auto linked = feature->getLinkedObject(false,nullptr,false,depth);
                if (linked == feature)
                    break;
                feature = linked;
                if (feature->getDocument() != doc) {
                    doc = feature->getDocument();
                    break;
                }
            }
            if(feature->isDerivedFrom(App::GeoFeature::getClassTypeId())) {
                auto owner = static_cast<App::GeoFeature*>(feature)->getElementOwner(element);
                if(owner)
                    doc = owner->getDocument();
            }
            obj = doc->getObjectByID(std::abs(tag));
        }
        if(!recursive) {
            ret.emplace_back(obj,original);
            ret.back().tag = tag;
            return ret;
        }
        if(!obj)
            break;
        if(element_type) {
            for(auto &hist : ret.back().intermediates) {
                if(shape.elementType(hist)!=element_type)
                    return ret;
            }
        }
        feature = obj;
        shape = Feature::getTopoShape(feature);
        element = original;
        if(element_type && shape.elementType(original)!=element_type)
            break;
    }while(feature);
    return ret;
}

QVector<Data::MappedElement>
Feature::getElementFromSource(App::DocumentObject *obj,
                              const char *subname,
                              App::DocumentObject *src,
                              const char *srcSub,
                              bool single)
{
    QVector<Data::MappedElement> res;
    if (!obj || !src)
        return res;

    auto shape = getTopoShape(obj, subname, false, nullptr, nullptr, true,
            /*transform = */ false);
    App::DocumentObject *owner = nullptr;
    auto srcShape = getTopoShape(src, srcSub, false, nullptr, &owner);
    int tagChanges;
    Data::MappedElement element;
    Data::IndexedName checkingSubname;
    std::string sub = Data::ComplexGeoData::noElementName(subname);
    auto checkHistory = [&](const Data::MappedName &name, size_t, long, long tag) {
        if (std::abs(tag) == owner->getID()) {
            if (!tagChanges)
                tagChanges = 1;
        } else if (tagChanges && ++tagChanges > 3) {
            // Once we found the tag, trace no more than 2 addition tag changes
            // to limited the search depth.
            return true;
        }
        if (name == element.name) {
            std::pair<std::string, std::string> objElement;
            std::size_t len = sub.size();
            checkingSubname.toString(sub);
            GeoFeature::resolveElement(obj, sub.c_str(), objElement);
            sub.resize(len);
            if (objElement.second.size()) {
                res.push_back(Data::MappedElement(Data::MappedName(objElement.first),
                                                  Data::IndexedName(objElement.second.c_str())));
                return true;
            }
        }
        return false;
    };

    // obtain both the old and new style element name
    std::pair<std::string, std::string> objElement;
    GeoFeature::resolveElement(src,srcSub,objElement,false);

    element.index = Data::IndexedName(objElement.second.c_str());
    if (!objElement.first.empty()) {
        // Strip prefix and indexed based name at the tail of the new style element name
        auto mappedName = Data::ComplexGeoData::newElementName(objElement.first.c_str());
        auto mapped = Data::ComplexGeoData::isMappedElement(mappedName.c_str());
        if (mapped)
            element.name = Data::MappedName(mapped);
    }

    // Translate the element name for datum
    if (objElement.second == "Plane")
        objElement.second = "Face1";
    else if (objElement.second == "Line")
        objElement.second = "Edge1";
    else if (objElement.second == "Point")
        objElement.second = "Vertex1";

    // Use the old style name to obtain the shape type
    auto type = TopoShape::shapeType(
            Data::ComplexGeoData::findElementName(objElement.second.c_str()), true);

    // If the given shape has the same number of sub shapes as the source (e.g.
    // a compound operation), then take a shortcut and assume the element index
    // remains the same. But we still need to trace the shape history to
    // confirm.
    if (type != TopAbs_SHAPE
            && element.name
            && shape.countSubShapes(type) == srcShape.countSubShapes(type)) {
        tagChanges = 0;
        checkingSubname = element.index;
        auto mapped = shape.getMappedName(element.index);
        shape.traceElement(mapped, checkHistory);
        if (res.size())
            return res;
    }

    // Try geometry search first
    auto subShape = getTopoShape(src, srcSub, /*needSubElement*/true);
    std::vector<std::string> names;
    shape.searchSubShape(subShape, &names);
    if (names.size()) {
        for (auto &name : names) {
            Data::MappedElement e;
            e.index = Data::IndexedName(name.c_str());
            e.name = shape.getMappedName(e.index, true);
            res.append(e);
            if (single)
                break;
        }
        return res;
    }

    if (!element.name || type == TopAbs_SHAPE)
        return res;

    // No shortcut, need to search every element of the same type. This may
    // result in multiple matches, e.g. a compound of array of the same
    // instance.
    const char *shapetype = TopoShape::shapeName(type).c_str();
    for (int i=0, count=shape.countSubShapes(type); i<count; ++i) {
        checkingSubname = Data::IndexedName::fromConst(shapetype, i+1);
        auto mapped = shape.getMappedName(checkingSubname);
        tagChanges = 0;
        shape.traceElement(mapped, checkHistory);
        if (single && res.size())
            break;
    }
    return res;
}

QVector<Data::MappedElement>
Feature::getRelatedElements(App::DocumentObject *obj, const char *name, bool sameType, bool withCache)
{
    auto owner = obj;
    auto shape = getTopoShape(obj,0,false,0,&owner); 
    QVector<Data::MappedElement> ret;
    Data::MappedElement mapped = shape.getElementName(name);
    if (!mapped.name)
        return ret;
    if(withCache && shape.getRelatedElementsCached(mapped.name,sameType,ret))
        return ret;
#if 0
    auto ret = shape.getRelatedElements(name,sameType); 
    if(ret.size()) {
        FC_LOG("topo shape returns " << ret.size() << " related elements");
        return ret;
    }
#endif

    char element_type = shape.elementType(mapped.name);
    TopAbs_ShapeEnum type = TopoShape::shapeType(element_type,true);
    if(type == TopAbs_SHAPE)
        return ret;

    auto source = getElementSource(owner,shape,mapped.name,sameType?element_type:0);
    for(auto &src : source) {
        auto srcIndex = shape.getIndexedName(src.second);
        if(srcIndex) {
            ret.push_back(Data::MappedElement(src.second,srcIndex));
            shape.cacheRelatedElements(mapped.name,sameType,ret);
            return ret;
        }
    }

    std::map<int,QVector<Data::MappedElement> > retMap;

    const char *shapetype = TopoShape::shapeName(type).c_str();
    std::ostringstream ss;
    for(size_t i=1;i<=shape.countSubShapes(type);++i) {
        Data::MappedElement related;
        related.index = Data::IndexedName::fromConst(shapetype, i);
        related.name = shape.getMappedName(related.index);
        if (!related.name)
            continue;
        auto src = getElementSource(owner,shape,related.name,sameType?element_type:0);
        int idx = (int)source.size()-1;
        for(auto rit=src.rbegin();idx>=0&&rit!=src.rend();++rit,--idx) {
            // TODO: shall we ignore source tag when comparing? It could cause
            // matching unrelated element, but it does help dealing with feature
            // reording in PartDesign::Body.
#if 0
            if(*rit != source[idx])
#else
            if(rit->second != source[idx].second)
#endif
            {
                ++idx;
                break;
            }
        }
        if(idx < (int)source.size())
            retMap[idx].push_back(related);
    }
    if(retMap.size())
        ret = retMap.begin()->second;
    shape.cacheRelatedElements(mapped.name,sameType,ret);
    return ret;
}

TopoDS_Shape Feature::getShape(const App::DocumentObject *obj, const char *subname, 
        bool needSubElement, Base::Matrix4D *pmat, App::DocumentObject **powner, 
        bool resolveLink, bool transform) 
{
    return getTopoShape(obj,subname,needSubElement,pmat,powner,resolveLink,transform,true).getShape();
}

static inline bool checkLink(const App::DocumentObject *obj) {
    return obj->getExtensionByType<App::LinkBaseExtension>(obj)
            || obj->getExtensionByType<App::GeoFeatureGroupExtension>(obj);
}

static bool checkLinkVisibility(std::set<std::string> &hiddens,
        bool check, const App::DocumentObject *&lastLink,
        const App::DocumentObject *obj, const char *subname)
{
    if(!obj || !obj->getNameInDocument())
        return false;

    if(checkLink(obj)) {
        lastLink = obj;
        for(auto &s : App::LinkBaseExtension::getHiddenSubnames(obj))
            hiddens.emplace(std::move(s));
    }

    if(!subname || !subname[0])
        return true;

    auto element = Data::ComplexGeoData::findElementName(subname);
    std::string sub(subname,element-subname);

    for(auto pos=sub.find('.');pos!=std::string::npos;pos=sub.find('.',pos+1)) {
        char c = sub[pos+1];
        sub[pos+1] = 0;

        for(auto it=hiddens.begin();it!=hiddens.end();) {
            if(!boost::starts_with(*it,CharRange(sub.c_str(),sub.c_str()+pos+1)))
                it = hiddens.erase(it);
            else {
                if(check && it->size()==pos+1)
                    return false;
                ++it;
            }
        }
        auto sobj = obj->getSubObject(sub.c_str());
        if(!sobj || !sobj->getNameInDocument())
            return false;
        if(checkLink(sobj)) {
            for(auto &s : App::LinkBaseExtension::getHiddenSubnames(sobj))
                hiddens.insert(std::string(sub)+s);
            lastLink = sobj;
        }
        sub[pos+1] = c;
    }

    std::set<std::string> res;
    for(auto &s : hiddens) {
        if(s.size()>sub.size())
            res.insert(s.c_str()+sub.size());
    }
    hiddens = std::move(res);
    return true;
}

static TopoShape _getTopoShape(const App::DocumentObject *obj, const char *subname, 
        bool needSubElement, Base::Matrix4D *pmat, App::DocumentObject **powner, 
        bool resolveLink, bool noElementMap, const std::set<std::string> hiddens,
        const App::DocumentObject *lastLink)

{
    TopoShape shape;

    if(!obj) return shape;

    PyObject *pyobj = 0;
    Base::Matrix4D mat;
    if(powner) *powner = 0;

    std::string _subname;
    auto subelement = Data::ComplexGeoData::findElementName(subname);
    if(!needSubElement && subname) {
        // strip out element name if not needed
        if(subelement && *subelement) {
            _subname = std::string(subname,subelement);
            subname = _subname.c_str();
        }
    }

    auto canCache = [&](const App::DocumentObject *o) {
        return !lastLink || 
            (hiddens.empty() && !App::GeoFeatureGroupExtension::isNonGeoGroup(o));
    };

    if(canCache(obj) && PropertyShapeCache::getShape(obj,shape,subname)) {
        if(noElementMap) {
            shape.resetElementMap();
            shape.Tag = 0;
            shape.Hasher.reset();
        }
    }

    App::DocumentObject *linked = 0;
    App::DocumentObject *owner = 0;
    Base::Matrix4D linkMat;
    App::StringHasherRef hasher;
    long tag;
    {
        Base::PyGILStateLocker lock;
        owner = obj->getSubObject(subname,shape.isNull()?&pyobj:0,&mat,false);
        if(!owner)
            return shape;
        tag = owner->getID();
        hasher = owner->getDocument()->getStringHasher();
        linked = owner->getLinkedObject(true,&linkMat,false);
        if(pmat) {
            if(resolveLink && obj!=owner)
                *pmat = mat * linkMat;
            else
                *pmat = mat;
        }
        if(!linked)
            linked = owner;
        if(powner) 
            *powner = resolveLink?linked:owner;

        if(!shape.isNull())
            return shape;

        if(pyobj && PyObject_TypeCheck(pyobj,&TopoShapePy::Type)) {
            shape = *static_cast<TopoShapePy*>(pyobj)->getTopoShapePtr();
            if(!shape.isNull()) {
                if(canCache(obj)) {
                    if(obj->getDocument() != linked->getDocument()
                            || mat.hasScale()
                            || (linked != owner && linkMat.hasScale()))
                        PropertyShapeCache::setShape(obj,shape,subname);
                }
                if(noElementMap) {
                    shape.resetElementMap();
                    shape.Tag = 0;
                    shape.Hasher.reset();
                }
                Py_DECREF(pyobj);
                return shape;
            }
        } else {
            if (linked->isDerivedFrom(App::Line::getClassTypeId())) {
                static TopoDS_Shape _shape;
                if (_shape.IsNull()) {
                    BRepBuilderAPI_MakeEdge builder(gp_Lin(gp_Pnt(0,0,0), gp_Dir(1,0,0)));
                    _shape = builder.Shape();
                    _shape.Infinite(Standard_True);
                }
                shape = TopoShape(tag, hasher, _shape);
            } else if (linked->isDerivedFrom(App::Plane::getClassTypeId())) {
                static TopoDS_Shape _shape;
                if (_shape.IsNull()) {
                    BRepBuilderAPI_MakeFace builder(gp_Pln(gp_Pnt(0,0,0), gp_Dir(0,0,1)));
                    _shape = builder.Shape();
                    _shape.Infinite(Standard_True);
                }
                shape = TopoShape(tag, hasher, _shape);
            } else if (linked->isDerivedFrom(App::Placement::getClassTypeId())) {
                auto element = Data::ComplexGeoData::findElementName(subname);
                if (element) {
                    if (boost::iequals("x", element) || boost::iequals("x-axis", element)
                            || boost::iequals("y", element) || boost::iequals("y-axis", element)
                            || boost::iequals("z", element) || boost::iequals("z-axis", element)) {
                        static TopoDS_Shape _shape;
                        if (_shape.IsNull()) {
                            BRepBuilderAPI_MakeEdge builder(gp_Lin(gp_Pnt(0,0,0), gp_Dir(0,0,1)));
                            _shape = builder.Shape();
                            _shape.Infinite(Standard_True);
                        }
                        shape = TopoShape(tag, hasher, _shape);
                    } else if (boost::iequals("o", element) || boost::iequals("origin", element)) {
                        static TopoDS_Shape _shape;
                        if (_shape.IsNull()) {
                            BRepBuilderAPI_MakeVertex builder(gp_Pnt(0,0,0));
                            _shape = builder.Shape();
                            _shape.Infinite(Standard_True);
                        }
                        shape = TopoShape(tag, hasher, _shape);
                    }
                }
                if (shape.isNull()) {
                    static TopoDS_Shape _shape;
                    if (_shape.IsNull()) {
                        BRepBuilderAPI_MakeFace builder(gp_Pln(gp_Pnt(0,0,0), gp_Dir(0,0,1)));
                        _shape = builder.Shape();
                        _shape.Infinite(Standard_True);
                    }
                    shape = TopoShape(tag, hasher, _shape);
                }
            }
            if (!shape.isNull()) {
                shape.transformShape(mat * linkMat,false,true);
                return shape;
            }
        }


        Py_XDECREF(pyobj);
    }

    // nothing can be done if there is sub-element references
    if(needSubElement && subelement && *subelement)
        return shape;

    if(obj!=owner) {
        if(canCache(owner) && PropertyShapeCache::getShape(owner,shape)) {
            bool scaled = shape.transformShape(mat,false,true);
            if(owner->getDocument()!=obj->getDocument()) {
                shape.reTagElementMap(obj->getID(),obj->getDocument()->getStringHasher());
                PropertyShapeCache::setShape(obj,shape,subname);
            } else if(scaled || (linked != owner && linkMat.hasScale()))
                PropertyShapeCache::setShape(obj,shape,subname);
        }
        if(!shape.isNull()) {
            if(noElementMap) {
                shape.resetElementMap();
                shape.Tag = 0;
                shape.Hasher.reset();
            }
            return shape;
        }
    }

    bool cacheable = true;

    auto link = owner->getExtensionByType<App::LinkBaseExtension>(true);
    if(owner!=linked 
            && (!link || (!link->_ChildCache.getSize() 
                            && link->getSubElements().size()<=1))) 
    {
        // if there is a linked object, and there is no child cache (which is used
        // for special handling of plain group), obtain shape from the linked object
        shape = Feature::getTopoShape(linked,0,false,0,0,false,false);
        if(shape.isNull())
            return shape;
        if(owner==obj)
            shape.transformShape(mat*linkMat,false,true);
        else
            shape.transformShape(linkMat,false,true);
        shape.reTagElementMap(tag,hasher);

    } else {
        // Construct a compound of sub objects
        std::vector<TopoShape> shapes;

        // Acceleration for link array. Unlike non-array link, a link array does
        // not return the linked object when calling getLinkedObject().
        // Therefore, it should be handled here.
        TopoShape baseShape;
        Base::Matrix4D baseMat;
        std::string op;
        if(link && link->getElementCountValue()) {
            linked = link->getTrueLinkedObject(false,&baseMat);
            if(linked && linked!=owner) {
                baseShape = Feature::getTopoShape(linked,0,false,0,0,false,false);
                if(!link->getShowElementValue())
                    baseShape.reTagElementMap(owner->getID(),owner->getDocument()->getStringHasher());
            }
        }
        for(auto &sub : owner->getSubObjects()) {
            if(sub.empty()) continue;
            int visible;
            std::string childName;
            App::DocumentObject *parent=0;
            Base::Matrix4D mat = baseMat;
            App::DocumentObject *subObj=0;
            if(sub.find('.')==std::string::npos)
                visible = 1;
            else {
                subObj = owner->resolve(sub.c_str(), &parent, &childName,0,0,&mat,false);
                if(!parent || !subObj)
                    continue;
                if(lastLink && App::GeoFeatureGroupExtension::isNonGeoGroup(parent))
                    visible = lastLink->isElementVisibleEx(childName.c_str());
                else
                    visible = parent->isElementVisibleEx(childName.c_str());
            }
            if(visible==0)
                continue;

            std::set<std::string> nextHiddens = hiddens;
            const App::DocumentObject *nextLink = lastLink;
            if(!checkLinkVisibility(nextHiddens,true,nextLink,owner,sub.c_str())) {
                cacheable = false;
                continue;
            }

            TopoShape shape;

            if(!subObj || baseShape.isNull() || mat.hasScale() < 0) {
                shape = _getTopoShape(owner,sub.c_str(),true,0,&subObj,false,false,nextHiddens,nextLink);
                if(shape.isNull())
                    continue;
                if(visible<0 && subObj && !subObj->Visibility.getValue())
                    continue;
            }else{
                if(link && !link->getShowElementValue())
                    shape = baseShape.makETransform(mat,(TopoShape::indexPostfix()+childName).c_str());
                else {
                    shape = baseShape.makETransform(mat);
                    shape.reTagElementMap(subObj->getID(),subObj->getDocument()->getStringHasher());
                }
            }
            shapes.push_back(shape);
        }

        if(shapes.empty()) 
            return shape;
        shape.Tag = tag;
        shape.Hasher = hasher;
        shape.makECompound(shapes);
    }

    if(cacheable && canCache(owner))
        PropertyShapeCache::setShape(owner,shape);

    if(owner!=obj) {
        bool scaled = shape.transformShape(mat,false,true);
        if(owner->getDocument()!=obj->getDocument()) {
            shape.reTagElementMap(obj->getID(),obj->getDocument()->getStringHasher());
            scaled = true; // force cache
        }
        if(canCache(obj) && scaled)
            PropertyShapeCache::setShape(obj,shape,subname);
    }
    if(noElementMap) {
        shape.resetElementMap();
        shape.Tag = 0;
        shape.Hasher.reset();
    }
    return shape;
}

TopoShape Feature::getTopoShape(const App::DocumentObject *obj, const char *subname, 
        bool needSubElement, Base::Matrix4D *pmat, App::DocumentObject **powner, 
        bool resolveLink, bool transform, bool noElementMap)
{
    if(!obj || !obj->getNameInDocument()) 
        return TopoShape();

    const App::DocumentObject *lastLink=0;
    std::set<std::string> hiddens;
    if(!checkLinkVisibility(hiddens,false,lastLink,obj,subname))
        return TopoShape();

    // NOTE! _getTopoShape() always return shape without top level
    // transformation for easy shape caching, i.e.  with `transform` set
    // to false. So we manually apply the top level transform if asked.

    if (needSubElement
            && (!pmat || *pmat == Base::Matrix4D()) 
            && obj->isDerivedFrom(Part::Feature::getClassTypeId())
            && !obj->hasExtension(App::LinkBaseExtension::getExtensionClassTypeId()))
    {
        // Some OCC shape making is very sensitive to shape transformation. So
        // check here if a direct sub shape is required, and bypass all extra
        // processing here.
        if(subname && *subname && Data::ComplexGeoData::findElementName(subname) == subname) {
            TopoShape ts = static_cast<const Part::Feature*>(obj)->Shape.getShape();
            if (!transform)
                ts.setShape(ts.getShape().Located(TopLoc_Location()),false);
            if (noElementMap)
                ts = ts.getSubShape(subname, true);
            else
                ts = ts.getSubTopoShape(subname, true);
            if (!ts.isNull()) {
                if (powner)
                    *powner = const_cast<App::DocumentObject*>(obj);
                if (pmat && transform)
                    *pmat = static_cast<const Part::Feature*>(obj)->Placement.getValue().toMatrix();
                return ts;
            }
        }
    }

    Base::Matrix4D mat;
    auto shape = _getTopoShape(obj, subname, needSubElement, &mat, 
            powner, resolveLink, noElementMap, hiddens, lastLink);

    if (needSubElement && shape.shapeType(true) == TopAbs_COMPOUND) {
        if (shape.countSubShapes(TopAbs_SOLID) == 1)
            shape = shape.getSubTopoShape(TopAbs_SOLID, 1);
        else if (shape.countSubShapes(TopAbs_COMPSOLID) == 1)
            shape = shape.getSubTopoShape(TopAbs_COMPSOLID, 1);
        else if (shape.countSubShapes(TopAbs_FACE) == 1)
            shape = shape.getSubTopoShape(TopAbs_FACE, 1);
        else if (shape.countSubShapes(TopAbs_SHELL) == 1)
            shape = shape.getSubTopoShape(TopAbs_SHELL, 1);
        else if (shape.countSubShapes(TopAbs_EDGE) == 1)
            shape = shape.getSubTopoShape(TopAbs_EDGE, 1);
        else if (shape.countSubShapes(TopAbs_WIRE) == 1)
            shape = shape.getSubTopoShape(TopAbs_WIRE, 1);
        else if (shape.countSubShapes(TopAbs_VERTEX) == 1)
            shape = shape.getSubTopoShape(TopAbs_VERTEX, 1);
    }

    Base::Matrix4D topMat;
    if(pmat || transform) {
        // Obtain top level transformation
        if(pmat)
            topMat = *pmat;
        if(transform)
            obj->getSubObject(0,0,&topMat);

        // Apply the top level transformation
        if(!shape.isNull())
            shape.transformShape(topMat,false,true);

        if(pmat)
            *pmat = topMat * mat;
    }

    return shape;

}

App::DocumentObject *Feature::getShapeOwner(const App::DocumentObject *obj, const char *subname)
{
    if(!obj) return 0;
    auto owner = obj->getSubObject(subname);
    if(owner) {
        auto linked = owner->getLinkedObject(true);
        if(linked)
            owner = linked;
    }
    return owner;
}

struct Feature::ElementCache {
    TopoShape shape;
    mutable std::vector<std::string> names;
    mutable bool searched;
};

void Feature::registerElementCache(const std::string &prefix, PropertyPartShape *prop)
{
    if (prop) {
        _elementCachePrefixMap.emplace_back(prefix, prop);
        return;
    }
    for (auto it=_elementCachePrefixMap.begin(); it!=_elementCachePrefixMap.end();) {
        if (it->first == prefix) {
            _elementCachePrefixMap.erase(it);
            break;
        }
    }
}

void Feature::onBeforeChange(const App::Property *prop) {
    PropertyPartShape *propShape = nullptr;
    const std::string *prefix = nullptr;
    if (prop == &Shape)
        propShape = &Shape;
    else {
        for (const auto &v :_elementCachePrefixMap) {
            if (prop == v.second) {
                prefix = &v.first;
                propShape = v.second;
            }
        }
    }
    if (propShape) {
        if (_elementCachePrefixMap.empty())
            _elementCache.clear();
        else {
            for (auto it=_elementCache.begin(); it!=_elementCache.end();) {
                bool remove;
                if (prefix)
                    remove = boost::starts_with(it->first, *prefix);
                else {
                    remove = true;
                    for (const auto &v : _elementCache) {
                        if (boost::starts_with(it->first, v.first)) {
                            remove = false;
                            break;
                        }
                    }
                }
                if (remove)
                    it = _elementCache.erase(it);
                else
                    ++it;
            }
        }
        if(getDocument() && !getDocument()->testStatus(App::Document::Restoring)
                         && !getDocument()->isPerformingTransaction())
        {
            std::vector<App::DocumentObject *> objs;
            std::vector<std::string> subs;
            for(auto prop : App::PropertyLinkBase::getElementReferences(this)) {
                if(!prop->getContainer())
                    continue;
                objs.clear();
                subs.clear();
                prop->getLinks(objs, true, &subs, false);
                for(auto &sub : subs) {
                    auto element = Data::ComplexGeoData::findElementName(sub.c_str());
                    if(!element || !element[0]
                                || Data::ComplexGeoData::hasMissingElement(element))
                        continue;
                    if (prefix) {
                        if (!boost::starts_with(element, *prefix))
                            continue;
                    } else {
                        bool found = false;
                        for (const auto &v : _elementCachePrefixMap) {
                            if (boost::starts_with(element, v.first)) {
                                found = true;
                                break;
                            }
                        }
                        if (found)
                            continue;
                    }
                    auto res = _elementCache.insert(
                            std::make_pair(std::string(element), ElementCache()));
                    if(res.second) {
                        res.first->second.searched = false;
                        res.first->second.shape = propShape->getShape().getSubTopoShape(
                                element + (prefix?prefix->size():0), true);
                    }
                }
            }
        }
    }
    GeoFeature::onBeforeChange(prop);
}

void Feature::onChanged(const App::Property* prop)
{
    // if the placement has changed apply the change to the point data as well
    if (prop == &this->Placement) {
        // The following code bypasses transaction, which may cause problem to
        // undo/redo
        //
        // TopoShape& shape = const_cast<TopoShape&>(this->Shape.getShape());
        // shape.setTransform(this->Placement.getValue().toMatrix());

        TopoShape shape = this->Shape.getShape();
        shape.setTransform(this->Placement.getValue().toMatrix());
        Base::ObjectStatusLocker<App::Property::Status, App::Property> guard(
                App::Property::NoRecompute, &this->Shape);
        this->Shape.setValue(shape);
    }
    // if the point data has changed check and adjust the transformation as well
    else if (prop == &this->Shape) {
        if (this->shouldApplyPlacement()) {
            this->Shape._Shape.setTransform(this->Placement.getValue().toMatrix());
        }
        else {
            Base::Placement p;
            // shape must not be null to override the placement
            if (!this->Shape.getValue().IsNull()) {
                p.fromMatrix(this->Shape.getShape().getTransform());
                this->Placement.setValueIfChanged(p);
            }
        }
    }

    GeoFeature::onChanged(prop);
}

bool Feature::shouldApplyPlacement()
{
    return isRecomputing();
}

const std::vector<std::string> &
Feature::searchElementCache(const std::string &element,
                            Data::SearchOptions options,
                            double tol,
                            double atol) const
{
    static std::vector<std::string> none;
    if(element.empty())
        return none;
    auto it = _elementCache.find(element);
    if(it == _elementCache.end() || it->second.shape.isNull())
        return none;
    if(!it->second.searched) {
        auto propShape = &Shape;
        const std::string *prefix = nullptr;
        for (const auto &v : _elementCachePrefixMap) {
            if (boost::starts_with(element, v.first)) {
                propShape = v.second;
                prefix = &v.first;
                break;
            }
        }
        it->second.searched = true;
        propShape->getShape().searchSubShape(
                it->second.shape, &it->second.names, options, tol, atol);
        if (prefix) {
            for (auto &name : it->second.names) {
                if (auto dot = strrchr(name.c_str(), '.'))
                    name.insert(dot+1-name.c_str(), *prefix);
                else
                    name.insert(0, *prefix);
            }
        }
    }
    return it->second.names;
}

TopLoc_Location Feature::getLocation() const
{
    Base::Placement pl = this->Placement.getValue();
    Base::Rotation rot(pl.getRotation());
    Base::Vector3d axis;
    double angle;
    rot.getValue(axis, angle);
    gp_Trsf trf;
    trf.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(axis.x, axis.y, axis.z)), angle);
    trf.SetTranslationPart(gp_Vec(pl.getPosition().x,pl.getPosition().y,pl.getPosition().z));
    return TopLoc_Location(trf);
}

    /// returns the type name of the ViewProvider
const char* Feature::getViewProviderName(void) const {
    return "PartGui::ViewProviderPart";
}

const App::PropertyComplexGeoData* Feature::getPropertyOfGeometry() const
{
    return &Shape;
}

const std::vector<const char *>& Feature::getElementTypes(bool all) const
{
    if (!all)
        return App::GeoFeature::getElementTypes();
    static std::vector<const char *> res;
    if (res.empty()) {
        res.reserve(8);
        res.push_back(TopoShape::shapeName(TopAbs_VERTEX).c_str());
        res.push_back(TopoShape::shapeName(TopAbs_EDGE).c_str());
        res.push_back(TopoShape::shapeName(TopAbs_WIRE).c_str());
        res.push_back(TopoShape::shapeName(TopAbs_FACE).c_str());
        res.push_back(TopoShape::shapeName(TopAbs_SHELL).c_str());
        res.push_back(TopoShape::shapeName(TopAbs_SOLID).c_str());
        res.push_back(TopoShape::shapeName(TopAbs_COMPSOLID).c_str());
        res.push_back(TopoShape::shapeName(TopAbs_COMPOUND).c_str());
    }
    return res;
}

Feature *Feature::create(const TopoShape &s, const char *name, App::Document *doc)
{
    if (!name || !name[0])
        name = "Shape";
    if (!doc) {
        doc = App::GetApplication().getActiveDocument();
        if (!doc)
            doc = App::GetApplication().newDocument();
    }
    auto res = static_cast<Part::Feature*>(doc->addObject("Part::Feature", name));
    res->Shape.setValue(s);
    res->purgeTouched();
    return res;
}

/*[[[cog
import PartParams
PartParams.define_properties()
]]]*/

// Auto generated code (Tools/params_utils.py:990)
App::PropertyLinkList *Part::Feature::getShapeContentsProperty(bool force)
{
    auto obj = this;
    if (auto prop = Base::freecad_dynamic_cast<App::PropertyLinkList>(
            obj->getPropertyByName("ShapeContents")))
    {
        if (prop->getContainer() == obj)
            return prop;
    }
    if (!force)
        return nullptr;
    return static_cast<App::PropertyLinkList*>(obj->addDynamicProperty(
            "App::PropertyLinkList", "ShapeContents", "ShapeContent",
    "Stores the expanded sub shape content objects",
    App::Prop_None));
}

// Auto generated code (Tools/params_utils.py:990)
App::PropertyBool *Part::Feature::getShapeContentSuppressedProperty(bool force)
{
    auto obj = this;
    if (auto prop = Base::freecad_dynamic_cast<App::PropertyBool>(
            obj->getPropertyByName("ShapeContentSuppressed")))
    {
        if (prop->getContainer() == obj)
            return prop;
    }
    if (!force)
        return nullptr;
    return static_cast<App::PropertyBool*>(obj->addDynamicProperty(
            "App::PropertyBool", "ShapeContentSuppressed", "ShapeContent",
    "Suppress this sub shape content",
    App::Prop_None));
}

// Auto generated code (Tools/params_utils.py:990)
App::PropertyLinkHidden *Part::Feature::getShapeContentReplacementProperty(bool force)
{
    auto obj = this;
    if (auto prop = Base::freecad_dynamic_cast<App::PropertyLinkHidden>(
            obj->getPropertyByName("ShapeContentReplacement")))
    {
        if (prop->getContainer() == obj)
            return prop;
    }
    if (!force)
        return nullptr;
    return static_cast<App::PropertyLinkHidden*>(obj->addDynamicProperty(
            "App::PropertyLinkHidden", "ShapeContentReplacement", "ShapeContent",
    "Refers to a shape replacement",
    App::Prop_None));
}

// Auto generated code (Tools/params_utils.py:990)
App::PropertyBool *Part::Feature::getShapeContentReplacementSuppressedProperty(bool force)
{
    auto obj = this;
    if (auto prop = Base::freecad_dynamic_cast<App::PropertyBool>(
            obj->getPropertyByName("ShapeContentReplacementSuppressed")))
    {
        if (prop->getContainer() == obj)
            return prop;
    }
    if (!force)
        return nullptr;
    return static_cast<App::PropertyBool*>(obj->addDynamicProperty(
            "App::PropertyBool", "ShapeContentReplacementSuppressed", "ShapeContent",
    "Suppress shape content replacement",
    App::Prop_None));
}

// Auto generated code (Tools/params_utils.py:990)
App::PropertyBool *Part::Feature::getShapeContentDetachedProperty(bool force)
{
    auto obj = this;
    if (auto prop = Base::freecad_dynamic_cast<App::PropertyBool>(
            obj->getPropertyByName("ShapeContentDetached")))
    {
        if (prop->getContainer() == obj)
            return prop;
    }
    if (!force)
        return nullptr;
    return static_cast<App::PropertyBool*>(obj->addDynamicProperty(
            "App::PropertyBool", "ShapeContentDetached", "ShapeContent",
    "If detached, than the shape content will not be auto removed and parent shape is removed",
    App::Prop_None));
}

// Auto generated code (Tools/params_utils.py:990)
App::PropertyLinkHidden *Part::Feature::get_ShapeContentOwnerProperty(bool force)
{
    auto obj = this;
    if (auto prop = Base::freecad_dynamic_cast<App::PropertyLinkHidden>(
            obj->getPropertyByName("_ShapeContentOwner")))
    {
        if (prop->getContainer() == obj)
            return prop;
    }
    if (!force)
        return nullptr;
    return static_cast<App::PropertyLinkHidden*>(obj->addDynamicProperty(
            "App::PropertyLinkHidden", "_ShapeContentOwner", "ShapeContent",
    "Refers to the shape owner",
    App::Prop_Hidden));
}
//[[[end]]]

void Feature::expandShapeContents()
{
    if (this->Shape.getShape().isNull())
        return;
    bool touched = isTouched() || isError();
    auto prop = getShapeContentsProperty();
    if (!prop)
        return;
    std::vector<App::DocumentObject*> objs = prop->getValues();
    std::size_t i = 0;
    int skip = 0;
    auto shape = this->Shape.getShape();
    for (const auto &s : shape.located(TopLoc_Location()).getSubTopoShapes(TopAbs_SHAPE)) {
        if (skip) {
            --skip;
            continue;
        }
        bool expandChild = false;
        Feature *feature = nullptr;
        for (;; ++i) {
            if (i < objs.size()) {
                if (auto feat = static_cast<Part::Feature*>(objs[i])) {
                    if (auto p = feat->getShapeContentSuppressedProperty()) {
                        if (p->getValue())
                            break;
                    }
                    if (auto p = feat->getShapeContentReplacementProperty()) {
                        auto suppressed = feat->getShapeContentReplacementSuppressedProperty();
                        if (p->getValue() && (!suppressed || !suppressed->getValue())) {
                            auto replacement = getTopoShape(p->getValue());
                            if (shape.shapeType(true) != TopAbs_COMPOUND
                                    && replacement.shapeType(true) == TopAbs_COMPOUND)
                            {
                                skip = replacement.countSubShapes(s.shapeType(true));
                                if (skip)
                                    --skip;
                            }
                            break;
                        }
                    }
                    if (auto p = feat->get_ShapeContentOwnerProperty()) {
                        if (p->getValue() == this)
                            feature = feat;
                    }
                }
                if (!feature)
                    break;
                if (feature->getShapeContentsProperty())
                    expandChild = true;
            } else {
                feature = static_cast<Part::Feature*>(this->getDocument()->addObject("Part::Feature", "Content"));
                feature->Visibility.setValue(false);
                feature->get_ShapeContentOwnerProperty(true)->setValue(this);
                std::string label = this->Label.getStrValue();
                if (!boost::ends_with(label, ":"))
                    label += ":";
                feature->Label.setValue(label + s.shapeName() + std::to_string(i+1) + ":");
                if (auto group = App::GeoFeatureGroupExtension::getGroupOfObject(this)) {
                    auto ext = group->getExtensionByType<App::GeoFeatureGroupExtension>();
                    ext->addObject(feature);
                }
            }
            break;
        }
        if (!feature)
            continue;
        if (i >= objs.size())
            objs.push_back(feature);
        feature->Shape.setStatus(App::Property::Transient, true);
        feature->Shape.setValue(s);
        ++i;
        if (expandChild)
            feature->expandShapeContents();
        feature->purgeTouched();
    }
    prop->setValues(objs);
    if (!touched)
        purgeTouched();
}

void Feature::beforeSave() const
{
    const_cast<Feature*>(this)->expandShapeContents();
    inherited::beforeSave();
}

void Feature::unsetupObject()
{
    collapseShapeContents(false);
    inherited::unsetupObject();
}

void Feature::collapseShapeContents(bool removeProperty)
{
    if (auto prop = getShapeContentsProperty()) {
        std::vector<std::string> removes;
        for (auto obj : prop->getValues()) {
            if (auto feat = Base::freecad_dynamic_cast<Part::Feature>(obj)) {
                if (auto prop = feat->get_ShapeContentOwnerProperty()) {
                    if (prop->getValue() == this
                            && (!feat->getShapeContentDetachedProperty()
                                || !feat->getShapeContentDetachedProperty()->getValue()))
                        removes.push_back(obj->getNameInDocument());
                }
            }
        }
        prop->setValues();
        if (removeProperty)
            removeDynamicProperty(prop->getName());
        for (const auto &name : removes)
            getDocument()->removeObject(name.c_str());
    }
}

void Feature::onDocumentRestored() {
    expandShapeContents();
    App::GeoFeature::onDocumentRestored();
}

void Feature::mergeShapeContents()
{
    auto prop = getShapeContentsProperty();
    if (!prop)
        return;
    TopoShape shape = Shape.getShape().located(TopLoc_Location());
    auto shapeType = shape.shapeType(true);
    auto subShapes = shape.getSubTopoShapes();
    std::vector<TopoShape> shapes;
    for (auto obj : prop->getValues()) {
        if (auto feat = Base::freecad_dynamic_cast<Part::Feature>(obj)) {
            if (auto prop = feat->getShapeContentSuppressedProperty()) {
                if (prop->getValue()) {
                    feat->Shape.setStatus(App::Property::Transient, false);
                    continue;
                }
            }
            else if (auto prop = feat->getShapeContentReplacementProperty()) {
                auto suppressed = feat->getShapeContentReplacementSuppressedProperty();
                if (prop->getValue() && (!suppressed || !suppressed->getValue())) {
                    auto replacement = getTopoShape(prop->getValue());
                    if (shape.shapeType(true) != TopAbs_COMPOUND
                            && replacement.shapeType(true) == TopAbs_COMPOUND
                            && subShapes.size())
                    {
                        auto replaces = replacement.getSubTopoShapes(subShapes[0].shapeType(true));
                        shapes.insert(shapes.end(), replaces.begin(), replaces.end());
                    }
                    else
                        shapes.push_back(replacement);
                    feat->Shape.setStatus(App::Property::Transient, false);
                    continue;
                }
            }
        }
        shapes.push_back(getTopoShape(obj));
    }

    bool changed = false;

    if (shapeType != TopAbs_WIRE && (shapeType != TopAbs_FACE || !shape.isPlanarFace())) {
        std::vector<int> removed, added;
        int i = -1;
        auto isSameShape = [](const TopoShape &a, const TopoShape &b) {
            if (!a.getShape().IsPartner(b.getShape()))
                return false;
            auto pla1 = a.getPlacement();
            auto pla2 = b.getPlacement();
            if (!pla1.getPosition().IsEqual(pla2.getPosition(), Precision::Confusion()))
                return false;
            return pla1.getRotation().isSame(pla1.getRotation(), 1e-12);
        };
        for (auto &subShape : subShapes) {
            ++i;
            bool found = false;
            for (const auto &s : shapes) {
                if (isSameShape(s, subShape)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                removed.push_back(i);
        }
        i = -1;
        for (const auto &s : shapes) {
            ++i;
            bool found = false;
            for (auto &subShape : subShapes) {
                if (isSameShape(s, subShape)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                added.push_back(i);
        }
        if (added.empty() && removed.empty())
            return;

        if (added.size() == removed.size()) {
            shape.setShape(BRepBuilderAPI_Copy(shape.getShape()), false);
            auto subShapes = shape.getSubTopoShapes();
            std::vector<std::pair<TopoShape, TopoShape>> replacements;
            std::size_t i = 0;
            for (; i<added.size(); ++i) {
                if (added[i] != removed[i])
                    break;
                replacements.emplace_back(subShapes[removed[i]], shapes[added[i]]);
            }
            shape = shape.replacEShape(replacements);
            changed = true;
        }
        else if (added.empty()) {
            shape.setShape(BRepBuilderAPI_Copy(shape.getShape()), false);
            auto subShapes = shape.getSubTopoShapes();
            std::vector<TopoShape> removedShape;
            for (int i : removed)
                removedShape.push_back(subShapes[i]);
            shape = shape.removEShape(removedShape);
            changed = true;
        }
    }

    switch (shapeType) {
    case TopAbs_VERTEX:
        if (!changed)
            throw Base::CADKernelError("Reshaping vertex is not supported");
        break;
    case TopAbs_FACE:
        if (!changed) {
            if (shape.isPlanarFace())
                shape.makEFace(shapes);
            else
                throw Base::CADKernelError("Reshaping non-planar face is not supported");
        }
        break;
    case TopAbs_WIRE:
        if (!changed)
            shape.makEWires(shapes);
        break;
    case TopAbs_EDGE:
        if (!changed)
            throw Base::CADKernelError("Reshaping edge is not supported");
        break;
    case TopAbs_SOLID:
        if (changed)
            shape = shape.makESolid();
        else
            shape.makESolid(shapes);
        break;
    case TopAbs_SHELL:
        if (changed)
            shape.makECompound(shape.getSubTopoShapes(TopAbs_FACE)).makEShell(false);
        else
            shape.makECompound(shapes).makEShell(false);
        break;
    case TopAbs_COMPSOLID:
        if (!changed)
            shape.makEBoolean(Part::OpCodes::Compsolid, shapes);
        break;
    default:
        if (!changed)
            shape.makECompound(shapes);
        break;
    }
    shape.setPlacement(Placement.getValue());
    this->Shape.setValue(shape);
    expandShapeContents();
}

static inline App::PropertyBool *propDisableMapping(App::PropertyContainer *container, bool forced)
{
    const char *name = "Part_NoElementMap";
    auto prop = Base::freecad_dynamic_cast<App::PropertyBool>(container->getPropertyByName(name));
    if (!prop || prop->getContainer() != container) {
        if (!forced)
            return nullptr;
        prop = static_cast<App::PropertyBool*>(
                container->addDynamicProperty("App::PropertyBool", name, "Part"));
    }
    return prop;
}

void Feature::disableElementMapping(App::PropertyContainer *container, bool disable)
{
    if (!container)
        return;
    auto prop = propDisableMapping(container, disable); // only force create if disable
    if (prop)
        prop->setValue(disable);
}

bool Feature::isElementMappingDisabled(App::PropertyContainer *container)
{
    if (!container)
        return false;
    auto prop = propDisableMapping(container, /*forced*/false);
    if (prop && prop->getValue())
        return true;
    if (auto obj = Base::freecad_dynamic_cast<App::DocumentObject>(container)) {
        if (auto doc = obj->getDocument()) {
            if (auto prop = propDisableMapping(doc, /*forced*/false))
                return prop->getValue();
        }
    }
    return false;
}

// ---------------------------------------------------------

PROPERTY_SOURCE(Part::FilletBase, Part::Feature)

FilletBase::FilletBase()
{
    ADD_PROPERTY(Base,(0));
    ADD_PROPERTY(Edges,(0,0,0));
    ADD_PROPERTY_TYPE(EdgeLinks,(0), 0, 
            (App::PropertyType)(App::Prop_ReadOnly|App::Prop_Hidden),0);
    Edges.setSize(0);
}

short FilletBase::mustExecute() const
{
    if (Base.isTouched() || Edges.isTouched() || EdgeLinks.isTouched())
        return 1;
    return 0;
}

void FilletBase::onChanged(const App::Property *prop) {
    if(getDocument() && !getDocument()->testStatus(App::Document::Restoring)) {
        if(prop == &Edges || prop == &Base) {
            if(!prop->testStatus(App::Property::User3))
                syncEdgeLink();
        }
    }
    Feature::onChanged(prop);
}

void FilletBase::onDocumentRestored() {
    if(EdgeLinks.getSubValues().empty())
        syncEdgeLink();
    Feature::onDocumentRestored();
}

void FilletBase::syncEdgeLink() {
    if(!Base.getValue() || !Edges.getSize()) {
        EdgeLinks.setValue(0);
        return;
    }
    std::vector<std::string> subs;
    std::string sub("Edge");
    for(auto &info : Edges.getValues()) 
        subs.emplace_back(sub+std::to_string(info.edgeid));
    EdgeLinks.setValue(Base.getValue(),subs);
}

void FilletBase::onUpdateElementReference(const App::Property *prop) {
    if(prop!=&EdgeLinks || !getNameInDocument())
        return;
    auto values = Edges.getValues();
    const auto &subs = EdgeLinks.getSubValues();
    for(size_t i=0;i<values.size();++i) {
        if(i>=subs.size()) {
            FC_WARN("fillet edge count mismatch in object " << getFullName());
            break;
        }
        int idx = 0;
        sscanf(subs[i].c_str(),"Edge%d",&idx);
        if(idx) 
            values[i].edgeid = idx;
        else
            FC_WARN("invalid fillet edge link '" << subs[i] << "' in object " 
                    << getFullName());
    }
    Edges.setStatus(App::Property::User3,true);
    Edges.setValues(values);
    Edges.setStatus(App::Property::User3,false);
}

// ---------------------------------------------------------

PROPERTY_SOURCE(Part::FeatureExt, Part::Feature)



namespace App {
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(Part::FeaturePython, Part::Feature)
template<> const char* Part::FeaturePython::getViewProviderName(void) const {
    return "PartGui::ViewProviderPython";
}
template<> PyObject* Part::FeaturePython::getPyObject(void) {
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new FeaturePythonPyT<Part::PartFeaturePy>(this),true);
    }
    return Py::new_reference_to(PythonObject);
}
/// @endcond

// explicit template instantiation
template class PartExport FeaturePythonT<Part::Feature>;
}

// ----------------------------------------------------------------
/*
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <gce_MakeLin.hxx>
#include <BRepIntCurveSurface_Inter.hxx>
#include <IntCurveSurface_IntersectionPoint.hxx>
#include <gce_MakeDir.hxx>
*/
std::vector<Part::cutFaces> Part::findAllFacesCutBy(
        const TopoShape& shape, const TopoShape& face, const gp_Dir& dir)
{
    // Find the centre of gravity of the face
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face.getShape(),props);
    gp_Pnt cog = props.CentreOfMass();

    // create a line through the centre of gravity
    gp_Lin line = gce_MakeLin(cog, dir);

    // Find intersection of line with all faces of the shape
    std::vector<cutFaces> result;
    BRepIntCurveSurface_Inter mkSection;
    // TODO: Less precision than Confusion() should be OK?

    for (mkSection.Init(shape.getShape(), line, Precision::Confusion()); mkSection.More(); mkSection.Next()) {
        gp_Pnt iPnt = mkSection.Pnt();
        double dsq = cog.SquareDistance(iPnt);

        if (dsq < Precision::Confusion())
            continue; // intersection with original face

        // Find out which side of the original face the intersection is on
        gce_MakeDir mkDir(cog, iPnt);
        if (!mkDir.IsDone())
            continue; // some error (appears highly unlikely to happen, though...)

        if (mkDir.Value().IsOpposite(dir, Precision::Confusion()))
            continue; // wrong side of face (opposite to extrusion direction)

        cutFaces newF;
        newF.face = mkSection.Face();
        newF.face.mapSubElement(shape);
        newF.distsq = dsq;
        result.push_back(newF);
    }

    return result;
}

bool Part::checkIntersection(const TopoDS_Shape& first, const TopoDS_Shape& second,
                             const bool quick, const bool touch_is_intersection) {

    Bnd_Box first_bb, second_bb;
    BRepBndLib::Add(first, first_bb);
    first_bb.SetGap(0);
    BRepBndLib::Add(second, second_bb);
    second_bb.SetGap(0);

    // Note: This test fails if the objects are touching one another at zero distance

    // Improving reliability: If it fails sometimes when touching and touching is intersection,
    // then please check further unless the user asked for a quick potentially unreliable result
    if (first_bb.IsOut(second_bb) && !touch_is_intersection)
        return false; // no intersection
    if (quick && !first_bb.IsOut(second_bb))
        return true; // assumed intersection

    // Try harder

    // This has been disabled because of:
    // https://www.freecadweb.org/tracker/view.php?id=3065

    //extrema method
    /*BRepExtrema_DistShapeShape extrema(first, second);
    if (!extrema.IsDone())
      return true;
    if (extrema.Value() > Precision::Confusion())
      return false;
    if (extrema.InnerSolution())
      return true;

    //here we should have touching shapes.
    if (touch_is_intersection)
    {

    //non manifold condition. 1 has to be a face
    for (int index = 1; index < extrema.NbSolution() + 1; ++index)
    {
        if (extrema.SupportTypeShape1(index) == BRepExtrema_IsInFace || extrema.SupportTypeShape2(index) == BRepExtrema_IsInFace)
            return true;
        }
      return false;
    }
    else
      return false;*/

    //boolean method.

    if (touch_is_intersection) {
        // If both shapes fuse to a single solid, then they intersect
        BRepAlgoAPI_Fuse mkFuse(first, second);
        if (!mkFuse.IsDone())
            return false;
        if (mkFuse.Shape().IsNull())
            return false;

        // Did we get one or two solids?
        TopExp_Explorer xp;
        xp.Init(mkFuse.Shape(),TopAbs_SOLID);
        if (xp.More()) {
            // At least one solid
            xp.Next();
            return (xp.More() == Standard_False);
        } else {
            return false;
        }
    } else {
        // If both shapes have common material, then they intersect
        BRepAlgoAPI_Common mkCommon(first, second);
        if (!mkCommon.IsDone())
            return false;
        if (mkCommon.Shape().IsNull())
            return false;

        // Did we get a solid?
        TopExp_Explorer xp;
        xp.Init(mkCommon.Shape(),TopAbs_SOLID);
        return (xp.More() == Standard_True);
    }

}


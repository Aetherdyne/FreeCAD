/****************************************************************************
 *   Copyright (c) 2018 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#include "ViewProviderExt.h"

// inclusion of the generated files (generated out of ViewProviderPartExtPy.xml)
#include "ViewProviderPartExtPy.h"
#include "ViewProviderPartExtPy.cpp"

using namespace PartGui;

// returns a string which represent the object e.g. when printed in python
std::string ViewProviderPartExtPy::representation(void) const
{
    return std::string("<Part::ViewProvider>");
}

PyObject *ViewProviderPartExtPy::getCustomAttributes(const char* ) const
{
    return 0;
}

int ViewProviderPartExtPy::setCustomAttributes(const char* , PyObject *)
{
    return 0;
}

Py::Dict ViewProviderPartExtPy::getElementColors() const {
    auto *vp = getViewProviderPartExtPtr();
    Py::Dict dict;
    for(auto &v : vp->getElementColors()) {
        Py::TupleN color(Py::Float(v.second.r),Py::Float(v.second.g),Py::Float(v.second.b));
        dict[Py::String(v.first)] = color;
    }
    return dict;
}

void ViewProviderPartExtPy::setElementColors(Py::Dict dict) {
    auto *vp = getViewProviderPartExtPtr();
    std::map<std::string,App::Color> info;
    for(auto it=dict.begin();it!=dict.end();++it) {
        const auto &value = *it;
        if(!value.first.isString())
            throw Py::TypeError("expect the key to be string");
        App::PropertyColor prop;
        try {
            prop.setPyObject(value.second.ptr());
        }catch(Base::Exception &e) {
            if(dynamic_cast<Base::TypeError*>(&e))
                throw Py::TypeError(e.what());
            throw Py::Exception(e.what());
        }
        info.emplace(Py::String(value.first).as_string(),prop.getValue());
    }
    vp->setElementColors(info);
}


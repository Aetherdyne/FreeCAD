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


#ifndef PART_FEATUREPARTFUSE_H
#define PART_FEATUREPARTFUSE_H

#include <App/PropertyLinks.h>

#include "FeaturePartBoolean.h"

namespace Part
{

class Fuse : public Boolean
{
    PROPERTY_HEADER(Part::Fuse);

public:
    Fuse();

    /** @name methods override Feature */
    //@{
    /// recalculate the Feature
protected:
    BRepAlgoAPI_BooleanOperation* makeOperation(const TopoDS_Shape&, const TopoDS_Shape&) const;
    const char *opCode() const;
    //@}
};

class MultiFuse : public Part::Feature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::MultiFuse);

public:
    MultiFuse();

    App::PropertyLinkList Shapes;
    PropertyShapeHistory History;
    App::PropertyBool Refine;

    /** @name methods override feature */
    //@{
    /// recalculate the Feature
    virtual App::DocumentObjectExecReturn *execute(void) override;
    virtual short mustExecute() const override;
    //@}
    /// returns the type name of the ViewProvider
    virtual const char* getViewProviderName(void) const override {
        return "PartGui::ViewProviderMultiFuse";
    }

    virtual App::PropertyLinkList *getShapeLinksProperty() override {return &Shapes;}
};

}

#endif // PART_FEATUREPARTFUSE_H

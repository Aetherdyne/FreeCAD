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


#ifndef GUI_DIALOG_PROPERTYPAGE_H
#define GUI_DIALOG_PROPERTYPAGE_H

#include <memory>
#include <boost/signals2.hpp>
#include <QTimer>
#include <QWidget>
#include <FCGlobal.h>
#include <App/Application.h>
#include <Base/Parameter.h>

namespace Gui {
namespace Dialog {

/** Base class for property pages.
 * \author Werner Mayer
 */
class GuiExport PropertyPage : public QWidget
{
    Q_OBJECT

public:
    PropertyPage(QWidget* parent = 0);
    virtual ~PropertyPage();

    bool isModified();
    void setModified(bool b);
    void onApply();
    void onCancel();
    void onReset();

protected:
    virtual void apply();
    virtual void cancel();
    virtual void reset();

private:
    bool bChanged; /**< for internal use only */

protected Q_SLOTS:
    virtual void loadSettings()=0;
    virtual void saveSettings()=0;
};

/** Base class for preferences pages.
 * \author Werner Mayer
 */
class GuiExport PreferencePage : public QWidget
{
    Q_OBJECT

public:
    PreferencePage(QWidget* parent = 0);
    virtual ~PreferencePage();

public Q_SLOTS:
    virtual void loadSettings()=0;
    virtual void saveSettings()=0;

protected:
    virtual void changeEvent(QEvent *e) = 0;
};

/** Subclass that embeds a form from a UI file.
 * \author Werner Mayer
 */
class GuiExport PreferenceUiForm : public PreferencePage
{
    Q_OBJECT

public:
    PreferenceUiForm(const QString& fn, QWidget* parent = 0);
    virtual ~PreferenceUiForm();

    void loadSettings();
    void saveSettings();

protected:
    void changeEvent(QEvent *e);

private:
    template <typename PW>
    void loadPrefWidgets();
    template <typename PW>
    void savePrefWidgets();

private:
    QWidget* form;
};

/** Base class for custom pages.
 * \author Werner Mayer
 */
class GuiExport CustomizeActionPage : public QWidget
{
    Q_OBJECT

public:
    CustomizeActionPage(QWidget* parent = 0);
    virtual ~CustomizeActionPage();

protected:
    bool event(QEvent* e);
    virtual void changeEvent(QEvent *e) = 0;

protected Q_SLOTS:
    virtual void onAddMacroAction(const QByteArray&)=0;
    virtual void onRemoveMacroAction(const QByteArray&)=0;
    virtual void onModifyMacroAction(const QByteArray&)=0;
};

} // namespace Dialog

/// Structure for storing a parameter key and its path to be used in std::map
struct GuiExport ParamKey {
    ParameterGrp::handle hGrp;
    const char *key;

    ParamKey(const char *path, const char *key)
        :hGrp(App::GetApplication().GetUserParameter().GetGroup(path))
        ,key(key)
    {}

    ParamKey(ParameterGrp *h, const char *key)
        :hGrp(h), key(key)
    {}

    bool operator < (const ParamKey &other) const {
        if (hGrp < other.hGrp)
            return true;
        if (hGrp > other.hGrp)
            return false;
        return strcmp(key, other.key) < 0;
    }
};

/// Helper class to handle parameter change
class GuiExport ParamHandler {
public:
    virtual ~ParamHandler() {}

    /** Called when the corresponding parameter key changes
     * @param key: the parameter key
     * @return Returns true if the handler needs to be delay triggered by a timer
     */
    virtual bool onChange(const ParamKey *key) = 0;
    /// Called in delay triggered
    virtual void onTimer() {}
};

/// Template class for a non-delayed parameter handler
template<class Func>
class ParamHandlerT : public ParamHandler
{
public:
    ParamHandlerT(Func f)
        :func(f)
    {}

    bool onChange(const ParamKey *key) override {
        func(key);
        return false;
    }

private:
    Func func;
};

/// Template class for a delayed parameter handler
template<class Func>
class ParamDelayedHandlerT : public ParamHandler
{
public:
    ParamDelayedHandlerT(Func f)
        :func(f)
    {}

    bool onChange(const ParamKey *) override {
        return true;
    }

    void onTimer() override {
        func();
    }

private:
    Func func;
};

// Helper class to manage handlers of a list of parameters.
//
// The handlers are stored in a map from ParamKey to shared pointer to a
// ParamHandler. The same handler can be registered with multiple keys. When
// the registered parameter key is changed, the manager will call the
// registered handler function ParamHandler::onChange(). If it returns True,
// then the handler will be appended to a queue to be invoked later by a timer
// to avoid repeatitive processing on change of multiple keys.
//
// The handler manager is initiated by the static function
// DlgGeneralImp::attachObserver(). It is intended to be one and only place of
// handling changes of the given set of parameters, regardless of whether the
// changes are coming from direct editing through parameter editor, user code
// changing of parameters, changing preference dialog, or loading a preference
// pack.
//
class GuiExport ParamHandlers {
public:
    ParamHandlers();
    virtual ~ParamHandlers();

    void addHandler(const ParamKey &key, const std::shared_ptr<ParamHandler> &handler);

    void addHandler(const char *path, const char *key, const std::shared_ptr<ParamHandler> &handler) {
        addHandler(ParamKey(path, key), handler);
    }

    void addHandler(ParameterGrp *hGrp, const char *key, const std::shared_ptr<ParamHandler> &handler) {
        addHandler(ParamKey(hGrp, key), handler);
    }

    void addHandler(const std::vector<ParamKey> &keys, const std::shared_ptr<ParamHandler> &handler) {
        for (const auto &key : keys)
            addHandler(key, handler);
    }

    void addHandler(const char *path, const std::vector<const char*> &keys, const std::shared_ptr<ParamHandler> &handler) {
        for (const auto &key : keys)
            addHandler(path, key, handler);
    }

    void addHandler(ParameterGrp *hGrp, const std::vector<const char*> &keys, const std::shared_ptr<ParamHandler> &handler) {
        for (const auto &key : keys)
            addHandler(hGrp, key, handler);
    }

    template<class Func>
    std::shared_ptr<ParamHandler> addHandler(const char *path, const char *key, Func func) {
        auto handler = std::shared_ptr<ParamHandler>(new ParamHandlerT(func));
        addHandler(path, key, handler);
        return handler;
    }

    template<class Func>
    std::shared_ptr<ParamHandler> addHandler(ParameterGrp *hGrp, const char *key, Func func) {
        auto handler = std::shared_ptr<ParamHandler>(new ParamHandlerT(func));
        addHandler(hGrp, key, handler);
        return handler;
    }

    template<class Func>
    std::shared_ptr<ParamHandler> addDelayedHandler(const char *path, const char *key, Func func) {
        auto hGrp = App::GetApplication().GetUserParameter().GetGroup(path);
        auto handler = std::shared_ptr<ParamHandler>(new ParamDelayedHandlerT(
            [hGrp, func]() {
                func(hGrp);
            }));
        addHandler(hGrp, key, handler);
        return handler;
    }

    template<class Func>
    std::shared_ptr<ParamHandler> addDelayedHandler(ParameterGrp *hGrp, const char *key, Func func) {
        auto handler = std::shared_ptr<ParamHandler>(new ParamDelayedHandlerT(
            [hGrp, func]() {
                func(hGrp);
            }));
        addHandler(hGrp, key, handler);
        return handler;
    }

    template<class Func>
    std::shared_ptr<ParamHandler> addDelayedHandler(const char *path,
                                                    const std::vector<const char *> &keys,
                                                    Func func)
    {
        auto hGrp = App::GetApplication().GetUserParameter().GetGroup(path);
        auto handler = std::shared_ptr<ParamHandler>(new ParamDelayedHandlerT(
            [hGrp, func]() {
                func(hGrp);
            }));
        for (const auto &key : keys)
            addHandler(ParamKey(hGrp, key), handler);
        return handler;
    }

    template<class Func>
    std::shared_ptr<ParamHandler> addDelayedHandler(ParameterGrp::handle hGrp,
                                                    const std::vector<const char *> &keys,
                                                    Func func)
    {
        auto handler = std::shared_ptr<ParamHandler>(new ParamDelayedHandlerT(
            [hGrp, func]() {
                func(hGrp);
            }));
        for (const auto &key : keys)
            addHandler(ParamKey(hGrp, key), handler);
        return handler;
    }

protected:
    std::map<ParamKey, std::shared_ptr<ParamHandler>> handlers;
    std::set<std::shared_ptr<ParamHandler>> pendings;
    boost::signals2::scoped_connection conn;
    QTimer timer;
};

} // namespace Gui

#endif // GUI_DIALOG_PROPERTYPAGE_H

#pragma once

#include "config.h"
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/signal-definitions.hpp>

#include "../view-impl.hpp"
#include "../toplevel-node.hpp"
#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/view.hpp"
#include "xwayland-helpers.hpp"
#include <wayfire/scene-operations.hpp>

#if WF_HAS_XWAYLAND

class wayfire_xwayland_view_base : public wf::toplevel_view_interface_t
{
  protected:
    wf::wl_listener_wrapper on_destroy, on_configure,
        on_set_title, on_set_app_id, on_or_changed, on_set_decorations,
        on_ping_timeout, on_set_window_type;

    wf::wl_listener_wrapper on_surface_commit;
    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface;

    wlr_xwayland_surface *xw;
    /** The geometry requested by the client */
    bool self_positioned = false;

    std::string title, app_id;
    /** Used by view implementations when the app id changes */
    void handle_app_id_changed(std::string new_app_id)
    {
        this->app_id = new_app_id;
        wf::emit_app_id_changed_signal(self());
    }

    std::string get_app_id() override
    {
        return this->app_id;
    }

    /** Used by view implementations when the title changes */
    void handle_title_changed(std::string new_title)
    {
        this->title = new_title;
        wf::emit_title_changed_signal(self());
    }

    std::string get_title() override
    {
        return this->title;
    }

    wlr_surface *get_keyboard_focus_surface() override
    {
        if (is_mapped() && priv->keyboard_focus_enabled)
        {
            return priv->wsurface;
        }

        return NULL;
    }

    bool has_type(xcb_atom_t type)
    {
        for (size_t i = 0; i < xw->window_type_len; i++)
        {
            if (xw->window_type[i] == type)
            {
                return true;
            }
        }

        return false;
    }

    bool is_dialog()
    {
        if (has_type(wf::xw::_NET_WM_WINDOW_TYPE_DIALOG) ||
            (xw->parent && (xw->window_type_len == 0)))
        {
            return true;
        } else
        {
            return false;
        }
    }

    /**
     * Determine whether the view should be treated as override-redirect or not.
     */
    bool is_unmanaged()
    {
        if (xw->override_redirect)
        {
            return true;
        }

        /** Example: Android Studio dialogs */
        if (xw->parent && !this->is_dialog() &&
            !this->has_type(wf::xw::_NET_WM_WINDOW_TYPE_NORMAL))
        {
            return true;
        }

        return false;
    }

    /**
     * Determine whether the view should be treated as a drag icon.
     */
    bool is_dnd()
    {
        return this->has_type(wf::xw::_NET_WM_WINDOW_TYPE_DND);
    }

    /**
     * Get the current implementation type.
     */
    virtual wf::xw::view_type get_current_impl_type() const = 0;

    bool has_client_decoration = true;
    void set_decoration_mode(bool use_csd)
    {
        bool was_decorated = should_be_decorated();
        this->has_client_decoration = use_csd;
        if ((was_decorated != should_be_decorated()) && is_mapped())
        {
            wf::view_decoration_state_updated_signal data;
            data.view = {this};

            this->emit(&data);
            wf::get_core().emit(&data);
        }
    }

    std::shared_ptr<wf::toplevel_view_node_t> surface_root_node;

  public:
    wayfire_xwayland_view_base(wlr_xwayland_surface *xww) : toplevel_view_interface_t(), xw(xww)
    {
        on_surface_commit.set_callback([&] (void*) { commit(); });
        surface_root_node = std::make_shared<wf::toplevel_view_node_t>(this);

        this->set_surface_root_node(std::make_shared<wf::toplevel_view_node_t>(this));
    }

    virtual void commit()
    {
        // TODO: this shoudl be done by the main node, likely unnecessary to do it here as well.
        wf::region_t dmg;
        wlr_surface_get_effective_damage(priv->wsurface, dmg.to_pixman());
        wf::scene::damage_node(this->get_surface_root_node(), dmg);
    }

    void check_create_main_surface(wlr_surface *surface, bool autocommit)
    {
        if (!this->main_surface)
        {
            this->main_surface = std::make_shared<wf::scene::wlr_surface_node_t>(surface, autocommit);
            priv->set_mapped_surface_contents(main_surface);
        }
    }

    virtual void map(wlr_surface *surface)
    {
        priv->set_mapped(true);
        check_create_main_surface(surface, true);
        on_surface_commit.connect(&surface->events.commit);
        if (role == wf::VIEW_ROLE_TOPLEVEL)
        {
            if (!parent)
            {
                wf::scene::readd_front(get_output()->wset()->get_node(), get_root_node());
                get_output()->wset()->add_view({this});
            }

            get_output()->focus_view(self(), true);
        }

        damage();
        emit_view_map();
        /* Might trigger repositioning */
        set_toplevel_parent(this->parent);
    }

    virtual void unmap()
    {
        damage();
        emit_view_pre_unmap();
        set_decoration(nullptr);

        main_surface = nullptr;
        priv->unset_mapped_surface_contents();
        on_surface_commit.disconnect();

        emit_view_unmap();
        priv->set_mapped(false);
    }

    virtual void initialize() override
    {
        wf::view_interface_t::initialize();

        // Set the output early, so that we can emit the signals on the output
        if (!get_output())
        {
            set_output(wf::get_core().get_active_output());
        }

        on_destroy.set_callback([&] (void*) { destroy(); });
        on_configure.set_callback([&] (void *data)
        {
            handle_client_configure((wlr_xwayland_surface_configure_event*)data);
        });
        on_set_title.set_callback([&] (void*)
        {
            handle_title_changed(nonull(xw->title));
        });
        on_set_app_id.set_callback([&] (void*)
        {
            handle_app_id_changed(nonull(xw->class_t));
        });
        on_or_changed.set_callback([&] (void*)
        {
            recreate_view();
        });
        on_set_decorations.set_callback([&] (void*)
        {
            update_decorated();
        });
        on_ping_timeout.set_callback([&] (void*)
        {
            wf::emit_ping_timeout_signal(self());
        });
        on_set_window_type.set_callback([&] (void*)
        {
            recreate_view();
        });

        handle_title_changed(nonull(xw->title));
        handle_app_id_changed(nonull(xw->class_t));
        update_decorated();

        on_destroy.connect(&xw->events.destroy);
        on_configure.connect(&xw->events.request_configure);
        on_set_title.connect(&xw->events.set_title);
        on_set_app_id.connect(&xw->events.set_class);
        on_or_changed.connect(&xw->events.set_override_redirect);
        on_ping_timeout.connect(&xw->events.ping_timeout);
        on_set_decorations.connect(&xw->events.set_decorations);
        on_set_window_type.connect(&xw->events.set_window_type);
    }

    /**
     * Destroy the view, and create a new one with the correct type -
     * unmanaged(override-redirect), DnD or normal.
     *
     * No-op if the view already has the correct type.
     */
    virtual void recreate_view();

    virtual void handle_client_configure(wlr_xwayland_surface_configure_event *ev)
    {}

    virtual void destroy()
    {
        this->xw = nullptr;

        on_destroy.disconnect();
        on_configure.disconnect();
        on_set_title.disconnect();
        on_set_app_id.disconnect();
        on_or_changed.disconnect();
        on_ping_timeout.disconnect();
        on_set_decorations.disconnect();
        on_set_window_type.disconnect();

        /* Drop the internal reference */
        unref();
    }

    virtual void ping() override
    {
        if (xw)
        {
            wlr_xwayland_surface_ping(xw);
        }
    }

    virtual bool should_be_decorated() override
    {
        return role == wf::VIEW_ROLE_TOPLEVEL && !has_client_decoration &&
               !has_type(wf::xw::_NET_WM_WINDOW_TYPE_SPLASH);
    }

    bool is_mapped() const override
    {
        return priv->wsurface != nullptr;
    }

    /* Translates geometry from X client configure requests to wayfire
     * coordinate system. The X coordinate system treats all outputs
     * as one big desktop, whereas wayfire treats the current workspace
     * of an output as 0,0 and everything else relative to that. This
     * means that we must take care when placing xwayland clients that
     * request a configure after initial mapping, while not on the
     * current workspace.
     *
     * @param output    The view's output
     * @param ws_offset The view's workspace minus the current workspace
     * @param geometry  The configure geometry as requested by the client
     *
     * @return Geometry with a position that is within the view's workarea.
     * The workarea is the workspace where the view was initially mapped.
     * Newly mapped views are placed on the current workspace.
     */
    wf::geometry_t translate_geometry_to_output(wf::output_t *output,
        wf::point_t ws_offset,
        wf::geometry_t g)
    {
        auto outputs = wf::get_core().output_layout->get_outputs();
        auto og   = output->get_layout_geometry();
        auto from = wf::get_core().output_layout->get_output_at(
            g.x + g.width / 2 + og.x, g.y + g.height / 2 + og.y);
        if (!from)
        {
            return g;
        }

        auto lg = from->get_layout_geometry();
        g.x += (og.x - lg.x) + ws_offset.x * og.width;
        g.y += (og.y - lg.y) + ws_offset.y * og.height;
        if (!this->is_mapped())
        {
            g.x *= (float)og.width / lg.width;
            g.y *= (float)og.height / lg.height;
        }

        return g;
    }

    virtual void configure_request(wf::geometry_t configure_geometry)
    {
        /* Wayfire positions views relative to their output, but Xwayland
         * windows have a global positioning. So, we need to make sure that we
         * always transform between output-local coordinates and global
         * coordinates. Additionally, when clients send a configure request
         * after they have already been mapped, we keep the view on the
         * workspace where its center point was from last configure, in
         * case the current workspace is not where the view lives */
        auto o = get_output();
        if (o)
        {
            auto view_workarea = (fullscreen ?
                o->get_relative_geometry() : o->workarea->get_workarea());
            auto og = o->get_layout_geometry();
            configure_geometry.x -= og.x;
            configure_geometry.y -= og.y;

            wayfire_toplevel_view view = {this};
            while (view->parent)
            {
                view = view->parent;
            }

            auto vg = view->get_wm_geometry();

            // View workspace relative to current workspace
            wf::point_t view_ws = {0, 0};
            if (view->is_mapped())
            {
                view_ws = {
                    (int)std::floor((vg.x + vg.width / 2.0) / og.width),
                    (int)std::floor((vg.y + vg.height / 2.0) / og.height),
                };

                view_workarea.x += og.width * view_ws.x;
                view_workarea.y += og.height * view_ws.y;
            }

            configure_geometry = translate_geometry_to_output(
                o, view_ws, configure_geometry);
            configure_geometry = wf::clamp(configure_geometry, view_workarea);
        }

        if (priv->frame)
        {
            configure_geometry =
                priv->frame->expand_wm_geometry(configure_geometry);
        }

        set_geometry(configure_geometry);
    }

    void update_decorated()
    {
        uint32_t csd_flags = WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE |
            WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER;
        this->set_decoration_mode(xw->decorations & csd_flags);
    }

    virtual void close() override
    {
        if (xw)
        {
            wlr_xwayland_surface_close(xw);
        }

        wf::view_interface_t::close();
    }

    void set_activated(bool active) override
    {
        if (xw)
        {
            wlr_xwayland_surface_activate(xw, active);
        }

        wf::toplevel_view_interface_t::set_activated(active);
    }
};

#endif

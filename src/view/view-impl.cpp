#include "view/surface-impl.hpp"
#include "wayfire/core.hpp"
#include "../core/core-impl.hpp"
#include "view-impl.hpp"
#include "wayfire/decorator.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/view.hpp"
#include "wayfire/workspace-set.hpp"
#include "wayfire/output-layout.hpp"
#include <memory>
#include <wayfire/util/log.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/scene-operations.hpp>

#include "xdg-shell.hpp"

wf::wlr_view_t::wlr_view_t() : wf::view_interface_t()
{
    on_surface_commit.set_callback([&] (void*) { commit(); });
}

void wf::wlr_view_t::handle_app_id_changed(std::string new_app_id)
{
    this->app_id = new_app_id;
    view_app_id_changed_signal data;
    data.view = self();
    emit(&data);
}

std::string wf::wlr_view_t::get_app_id()
{
    return this->app_id;
}

void wf::wlr_view_t::handle_title_changed(std::string new_title)
{
    this->title = new_title;
    view_title_changed_signal data;
    data.view = self();
    emit(&data);
}

std::string wf::wlr_view_t::get_title()
{
    return this->title;
}

void wf::wlr_view_t::set_position(int x, int y,
    wf::geometry_t old_geometry, bool send_signal)
{
    auto obox = get_output_geometry();
    auto wm   = get_wm_geometry();

    view_geometry_changed_signal data;
    data.view = self();
    data.old_geometry = old_geometry;

    view_damage_raw(self(), last_bounding_box);
    /* obox.x - wm.x is the current difference in the output and wm geometry */
    geometry.x = x + obox.x - wm.x;
    geometry.y = y + obox.y - wm.y;

    /* Make sure that if we move the view while it is unmapped, its snapshot
     * is still valid coordinates */
    priv->offscreen_buffer = priv->offscreen_buffer.translated({
        x - data.old_geometry.x, y - data.old_geometry.y,
    });

    damage();

    if (send_signal)
    {
        emit(&data);
        wf::get_core().emit(&data);
        if (get_output())
        {
            get_output()->emit(&data);
        }
    }

    last_bounding_box = get_bounding_box();
    scene::update(this->get_surface_root_node(), scene::update_flag::GEOMETRY);
}

void wf::wlr_view_t::move(int x, int y)
{
    set_position(x, y, get_wm_geometry(), true);
}

void wf::wlr_view_t::adjust_anchored_edge(wf::dimensions_t new_size)
{
    if (priv->edges)
    {
        auto wm = get_wm_geometry();
        if (priv->edges & WLR_EDGE_LEFT)
        {
            wm.x += geometry.width - new_size.width;
        }

        if (priv->edges & WLR_EDGE_TOP)
        {
            wm.y += geometry.height - new_size.height;
        }

        set_position(wm.x, wm.y,
            get_wm_geometry(), false);
    }
}

void wf::wlr_view_t::update_size()
{
    if (!is_mapped())
    {
        return;
    }

    auto current_size = get_size();
    if ((current_size.width == geometry.width) &&
        (current_size.height == geometry.height))
    {
        return;
    }

    /* Damage current size */
    view_damage_raw(self(), last_bounding_box);
    adjust_anchored_edge(current_size);

    view_geometry_changed_signal data;
    data.view = self();
    data.old_geometry = get_wm_geometry();

    geometry.width  = current_size.width;
    geometry.height = current_size.height;

    /* Damage new size */
    last_bounding_box = get_bounding_box();
    view_damage_raw(self(), last_bounding_box);
    emit(&data);
    wf::get_core().emit(&data);
    if (get_output())
    {
        get_output()->emit(&data);
    }

    scene::update(this->get_surface_root_node(), scene::update_flag::GEOMETRY);
}

bool wf::wlr_view_t::should_resize_client(
    wf::dimensions_t request, wf::dimensions_t current_geometry)
{
    /*
     * Do not send a configure if the client will retain its size.
     * This is needed if a client starts with one size and immediately resizes
     * again.
     *
     * If we do configure it with the given size, then it will think that we
     * are requesting the given size, and won't resize itself again.
     */
    if (this->last_size_request == wf::dimensions_t{0, 0})
    {
        return request != current_geometry;
    } else
    {
        return request != last_size_request;
    }
}

wf::geometry_t wf::wlr_view_t::get_output_geometry()
{
    return geometry;
}

wf::geometry_t wf::wlr_view_t::get_wm_geometry()
{
    if (priv->frame)
    {
        return priv->frame->expand_wm_geometry(geometry);
    } else
    {
        return geometry;
    }
}

wlr_surface*wf::wlr_view_t::get_keyboard_focus_surface()
{
    if (is_mapped() && priv->keyboard_focus_enabled)
    {
        return priv->wsurface;
    }

    return NULL;
}

bool wf::wlr_view_t::should_be_decorated()
{
    return role == wf::VIEW_ROLE_TOPLEVEL && !has_client_decoration;
}

void wf::wlr_view_t::set_decoration_mode(bool use_csd)
{
    bool was_decorated = should_be_decorated();
    this->has_client_decoration = use_csd;
    if ((was_decorated != should_be_decorated()) && is_mapped())
    {
        wf::view_decoration_state_updated_signal data;
        data.view = self();

        this->emit(&data);
        wf::get_core().emit(&data);
    }
}

void wf::wlr_view_t::commit()
{
    wf::region_t dmg;
    wlr_surface_get_effective_damage(priv->wsurface, dmg.to_pixman());
    wf::scene::damage_node(this->get_surface_root_node(), dmg);

    update_size();

    /* Clear the resize edges.
     * This is must be done here because if the user(or plugin) resizes too fast,
     * the shell client might still haven't configured the surface, and in this
     * case the next commit(here) needs to still have access to the gravity */
    if (!priv->in_continuous_resize)
    {
        priv->edges = 0;
    }

    this->last_bounding_box = get_bounding_box();
}

void wf::wlr_view_t::map(wlr_surface *surface)
{
    on_surface_commit.connect(&surface->events.commit);
    this->main_surface = std::make_shared<scene::wlr_surface_node_t>(surface, true);
    priv->set_mapped_surface_contents(main_surface);
    priv->set_mapped(true);

    update_size();

    if (role == VIEW_ROLE_TOPLEVEL)
    {
        if (!parent)
        {
            wf::scene::readd_front(get_output()->wset()->get_node(), get_root_node());
            get_output()->wset()->add_view(self());
        }

        get_output()->focus_view(self(), true);
    }

    damage();
    emit_view_map();
    /* Might trigger repositioning */
    set_toplevel_parent(this->parent);
}

void wf::wlr_view_t::unmap()
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

void wf::emit_view_map_signal(wayfire_view view, bool has_position)
{
    wf::view_mapped_signal data;
    data.view = view;
    data.is_positioned = has_position;

    view->emit(&data);
    view->get_output()->emit(&data);
    wf::get_core().emit(&data);
}

void wf::emit_ping_timeout_signal(wayfire_view view)
{
    wf::view_ping_timeout_signal data;
    data.view = view;
    view->emit(&data);
}

void wf::emit_geometry_changed_signal(wayfire_view view, wf::geometry_t old_geometry)
{
    wf::view_geometry_changed_signal data;
    data.view = view;
    data.old_geometry = old_geometry;

    view->emit(&data);
    wf::get_core().emit(&data);
    if (view->get_output())
    {
        view->get_output()->emit(&data);
    }
}

void wf::view_interface_t::emit_view_map()
{
    emit_view_map_signal(self(), false);
}

void wf::view_interface_t::emit_view_unmap()
{
    view_unmapped_signal data;
    data.view = self();

    if (get_output())
    {
        get_output()->emit(&data);

        view_disappeared_signal disappeared_data;
        disappeared_data.view = self();
        get_output()->emit(&disappeared_data);
    }

    this->emit(&data);
    wf::get_core().emit(&data);
}

void wf::view_interface_t::emit_view_pre_unmap()
{
    view_pre_unmap_signal data;
    data.view = self();

    if (get_output())
    {
        get_output()->emit(&data);
    }

    emit(&data);
    wf::get_core().emit(&data);
}

void wf::wlr_view_t::destroy()
{
    /* Drop the internal reference */
    unref();
}

void wf::init_desktop_apis()
{
    init_xdg_shell();
    init_layer_shell();

    wf::option_wrapper_t<bool> xwayland_enabled("core/xwayland");
    if (xwayland_enabled == 1)
    {
        init_xwayland();
    }
}

wayfire_view wf::wl_surface_to_wayfire_view(wl_resource *resource)
{
    if (!resource)
    {
        return nullptr;
    }

    auto surface = (wlr_surface*)wl_resource_get_user_data(resource);
    if (!surface)
    {
        return nullptr;
    }

    void *handle = NULL;
    if (wlr_surface_is_xdg_surface(surface))
    {
        handle = wlr_xdg_surface_from_wlr_surface(surface)->data;
    }

    if (wlr_surface_is_layer_surface(surface))
    {
        handle = wlr_layer_surface_v1_from_wlr_surface(surface)->data;
    }

#if WF_HAS_XWAYLAND
    if (wlr_surface_is_xwayland_surface(surface))
    {
        handle = wlr_xwayland_surface_from_wlr_surface(surface)->data;
    }

#endif

    wf::view_interface_t *view = static_cast<wf::wlr_view_t*>(handle);
    return view ? view->self() : nullptr;
}

wf::dimensions_t wf::wlr_view_t::get_size() const
{
    if (!is_mapped())
    {
        return {0, 0};
    }

    return {
        priv->wsurface->current.width,
        priv->wsurface->current.height,
    };
}

bool wf::wlr_view_t::is_mapped() const
{
    return priv->wsurface != nullptr;
}

wlr_buffer*wf::wlr_view_t::get_buffer()
{
    if (priv->wsurface && wlr_surface_has_buffer(priv->wsurface))
    {
        return &priv->wsurface->buffer->base;
    }

    return nullptr;
}

void wf::view_interface_t::view_priv_impl::set_mapped_surface_contents(
    std::shared_ptr<scene::wlr_surface_node_t> content)
{
    wsurface = content->get_surface();
    surface_root_node->set_children_list({content});
    scene::update(surface_root_node, scene::update_flag::CHILDREN_LIST);

    if (content->get_surface())
    {
        surface_controller =
            std::make_unique<wlr_surface_controller_t>(content->get_surface(), surface_root_node);
    }
}

void wf::view_interface_t::view_priv_impl::unset_mapped_surface_contents()
{
    wsurface = nullptr;
    surface_root_node->set_children_list({});
    scene::update(surface_root_node, scene::update_flag::CHILDREN_LIST);
    surface_controller.reset();
}

void wf::view_interface_t::view_priv_impl::set_mapped(bool mapped)
{
    if (mapped)
    {
        scene::set_node_enabled(root_node, true);
    } else
    {
        scene::set_node_enabled(root_node, false);
    }
}

// ---------------------------------------------- view helpers -----------------------------------------------
std::optional<wf::scene::layer> wf::get_view_layer(wayfire_view view)
{
    wf::scene::node_t *node = view->get_root_node().get();
    auto root = wf::get_core().scene().get();

    while (node->parent())
    {
        if (node->parent() == root)
        {
            for (int i = 0; i < (int)wf::scene::layer::ALL_LAYERS; i++)
            {
                if (node == root->layers[i].get())
                {
                    return (wf::scene::layer)i;
                }
            }
        }

        node = node->parent();
    }

    return {};
}

void wf::view_bring_to_front(wayfire_view view)
{
    wf::scene::node_t *node = view->get_root_node().get();
    wf::scene::node_t *damage_from = nullptr;
    while (node->parent())
    {
        if (!node->is_structure_node() && dynamic_cast<scene::floating_inner_node_t*>(node->parent()))
        {
            damage_from = node->parent();
            wf::scene::raise_to_front(node->shared_from_this());
        }

        node = node->parent();
    }

    if (damage_from)
    {
        wf::scene::damage_node(damage_from->shared_from_this(), damage_from->get_bounding_box());
    }
}

static void gather_views(wf::scene::node_ptr root, std::vector<wayfire_view>& views)
{
    if (!root->is_enabled())
    {
        return;
    }

    if (auto view = wf::node_to_view(root))
    {
        views.push_back(view);
        return;
    }

    for (auto& ch : root->get_children())
    {
        gather_views(ch, views);
    }
}

std::vector<wayfire_view> wf::collect_views_from_scenegraph(wf::scene::node_ptr root)
{
    std::vector<wayfire_view> views;
    gather_views(root, views);
    return views;
}

std::vector<wayfire_view> wf::collect_views_from_output(wf::output_t *output,
    std::initializer_list<wf::scene::layer> layers)
{
    std::vector<wayfire_view> views;
    for (auto layer : layers)
    {
        gather_views(output->node_for_layer(layer), views);
    }

    return views;
}

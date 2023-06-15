#ifndef VIEW_IMPL_HPP
#define VIEW_IMPL_HPP

#include <memory>
#include <wayfire/nonstd/safe-list.hpp>
#include <wayfire/view.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/toplevel.hpp>

#include "surface-impl.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/unstable/wlr-surface-node.hpp"
#include "wayfire/output.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/util.hpp"
#include "wayfire/view-transform.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/toplevel-view.hpp>

struct wlr_seat;
namespace wf
{
/** Private data used by the default view_interface_t implementation */
class view_interface_t::view_priv_impl
{
  public:
    wlr_surface *wsurface = nullptr;

    /** Reference count to the view */
    int ref_cnt = 0;

    size_t last_view_cnt = 0;

    bool keyboard_focus_enabled = true;
    std::unique_ptr<wf::decorator_frame_t_t> frame = nullptr;

    uint32_t allowed_actions = VIEW_ALLOW_ALL;

    uint32_t edges = 0;
    wlr_box minimize_hint = {0, 0, 0, 0};

    scene::floating_inner_ptr root_node;
    std::shared_ptr<scene::transform_manager_node_t> transformed_node;
    std::unique_ptr<wlr_surface_controller_t> surface_controller;
    scene::floating_inner_ptr surface_root_node;
    wf::output_t *output;

    void set_mapped(bool mapped);
    void set_mapped_surface_contents(std::shared_ptr<scene::wlr_surface_node_t> content);
    void unset_mapped_surface_contents();
    std::weak_ptr<wf::workspace_set_t> current_wset;

    std::shared_ptr<toplevel_t> toplevel;
};

/**
 * Adjust the position of the view according to the new size of its buffer and the geometry.
 */
void adjust_geometry_for_gravity(wf::toplevel_state_t& desired_state, wf::dimensions_t actual_size);

/** Emit the map signal for the given view */
void emit_view_map_signal(wayfire_view view, bool has_position);
void emit_ping_timeout_signal(wayfire_view view);
void emit_geometry_changed_signal(wayfire_toplevel_view view, wf::geometry_t old_geometry);

void emit_title_changed_signal(wayfire_view view);
void emit_app_id_changed_signal(wayfire_view view);

void init_xdg_shell();
void init_xwayland();
void init_layer_shell();

std::string xwayland_get_display();
void xwayland_update_default_cursor();

/* Ensure that the given surface is on top of the Xwayland stack order. */
void xwayland_bring_to_front(wlr_surface *surface);

void init_desktop_apis();
void init_xdg_decoration_handlers();
}

#endif /* end of include guard: VIEW_IMPL_HPP */

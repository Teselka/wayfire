#pragma once

#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
namespace wf
{
/**
 * An interface which describes basic window management operations on toplevels.
 */
class window_manager_t
{
  public:
    virtual ~window_manager_t() = default;

    /**
     * Update the remembered last windowed geometry.
     *
     * When a view is being tiled or fullscreened, we usually want to remember its size and position so that
     * it can be restored to that geometry after unfullscreening/untiling. window-manager implementations keep
     * track of this when a plugin calls update_last_windowed_geometry().
     */
    virtual void update_last_windowed_geometry(wayfire_toplevel_view view);

    /**
     * Get the stored last_windowed_geometry, if it was stored at all.
     */
    virtual std::optional<wf::geometry_t> get_last_windowed_geometry(wayfire_toplevel_view view);

    /**
     * Request that an interactive move starts for the given view.
     */
    virtual void move_request(wayfire_toplevel_view view);

    /**
     * Request that an interactive resize starts for the given view.
     */
    virtual void resize_request(wayfire_toplevel_view view, uint32_t edges = 0);

    /**
     * Try to focus the view and its output.
     */
    virtual void focus_request(wayfire_toplevel_view view);

    /** Request that the view is (un)minimized */
    virtual void minimize_request(wayfire_toplevel_view view, bool minimized);

    /**
     * Request that the view is (un)tiled on the given workspace of its primary output.
     *
     * Note: by default, any tiled edges means that the view gets the full workarea.
     *
     * @param ws If no workspace is provided, the view will be tiled on the current workspace. Otherwise,
     *   the view will be tiled on the provided workspace.
     */
    virtual void tile_request(wayfire_toplevel_view view, uint32_t tiled_edges,
        std::optional<wf::point_t> ws = {});

    /**
     * Request that the view is (un)fullscreened on the given workspace of its primary output.
     *
     * @param ws If no workspace is provided, the view will be fullscreened or restored to the current
     *   workspace of its primary output. Otherwise, the operation will be done for the given workspace.
     */
    virtual void fullscreen_request(wayfire_toplevel_view view, wf::output_t *output, bool state,
        std::optional<wf::point_t> ws = {});
};
}

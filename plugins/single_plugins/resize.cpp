#include "wayfire/geometry.hpp"
#include "wayfire/plugins/common/input-grab.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/txn/transaction-manager.hpp"
#include <wayfire/toplevel.hpp>
#include <cmath>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/view.hpp>
#include <wayfire/core.hpp>
#include <wayfire/workspace-set.hpp>
#include <linux/input.h>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/wobbly/wobbly-signal.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wlr/util/edges.h>
#include <wayfire/plugins/common/key-repeat.hpp>
#include "plugins/ipc/ipc-activator.hpp"
#include "plugins/ipc/ipc.hpp"

class wayfire_resize : public wf::per_output_plugin_instance_t, public wf::pointer_interaction_t,
    public wf::touch_interaction_t, public wf::keyboard_interaction_t
{
    wf::signal::connection_t<wf::view_resize_request_signal> on_resize_request =
        [=] (wf::view_resize_request_signal *request)
    {
        if (!request->view)
        {
            return;
        }

        auto touch = wf::get_core().get_touch_position(0);
        if (!std::isnan(touch.x) && !std::isnan(touch.y))
        {
            is_using_touch = true;
        } else
        {
            is_using_touch = false;
        }

        is_using_keyboard = false;
        exit_on_click     = true;
        preserve_aspect   = false;
        initiate(request->view, request->edges);
    };

    wf::signal::connection_t<wf::view_disappeared_signal> on_view_disappeared =
        [=] (wf::view_disappeared_signal *ev)
    {
        if (ev->view == view)
        {
            view = nullptr;
            input_pressed(WLR_BUTTON_RELEASED);
        }
    };

    wf::button_callback activate_binding;
    wf::button_callback activate_binding_preserve_aspect;
    wf::key_callback activate_key_binding;
    wf::key_repeat_t key_repeat;

    wayfire_toplevel_view view;

    bool is_active = false;
    bool is_using_touch, is_using_keyboard;
    bool exit_on_click; /* instead of was_client_request */
    bool preserve_aspect = false;
    wf::point_t grab_start;
    wf::point_t last_input;
    wf::geometry_t grabbed_geometry;
    wf::point_t key_diff; /* amount of change in size from keyboard interaction */

    uint32_t current_key;
    uint32_t edges;
    wf::option_wrapper_t<wf::buttonbinding_t> button{"resize/activate"};
    wf::option_wrapper_t<wf::buttonbinding_t> button_preserve_aspect{
        "resize/activate_preserve_aspect"};
    wf::option_wrapper_t<int> step{"resize/step"};
    std::unique_ptr<wf::input_grab_t> input_grab;
    wf::plugin_activation_data_t grab_interface = {
        .name = "resize",
        .capabilities = wf::CAPABILITY_GRAB_INPUT | wf::CAPABILITY_MANAGE_DESKTOP,
    };

  public:
    void init() override
    {
        input_grab = std::make_unique<wf::input_grab_t>("resize", output, this, this, this);

        activate_binding = [=] (auto)
        {
            return activate(false);
        };

        activate_binding_preserve_aspect = [=] (auto)
        {
            return activate(true);
        };

        output->add_button(button, &activate_binding);
        output->add_button(button_preserve_aspect, &activate_binding_preserve_aspect);
        grab_interface.cancel = [=] ()
        {
            deactivate();
        };

        output->connect(&on_resize_request);
        output->connect(&on_view_disappeared);
    }

    bool activate(bool should_preserve_aspect)
    {
        auto view = toplevel_cast(wf::get_core().get_cursor_focus_view());
        if (view && !is_active)
        {
            is_using_touch    = false;
            exit_on_click     = false;
            is_using_keyboard = false;
            preserve_aspect   = should_preserve_aspect;
            initiate(view);
        }

        return false;
    }

    bool activate_key_ipc(bool allow_keyboard, bool preserve_aspect_, wayfire_view target_view)
    {
        auto target_toplevel = toplevel_cast(target_view);
        if (is_active)
        {
            if (target_toplevel && (target_toplevel != this->view))
            {
                deactivate();
            } else if (is_using_keyboard)
            {
                deactivate();
                return true;
            }
        }

        if (!target_toplevel)
        {
            target_toplevel = toplevel_cast(wf::get_core().seat->get_active_view());
        }

        if (target_toplevel)
        {
            is_using_touch    = false;
            exit_on_click     = true;
            is_using_keyboard = allow_keyboard;
            preserve_aspect   = preserve_aspect_;
            current_key = 0;
            initiate(target_toplevel, WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM);
            return true;
        }

        return false;
    }

    void handle_pointer_button(const wlr_pointer_button_event& event) override
    {
        if ((event.state == WLR_BUTTON_RELEASED) && exit_on_click && (event.button == BTN_LEFT))
        {
            return input_pressed(event.state);
        }

        if ((event.button != wf::buttonbinding_t(button).get_button()) &&
            (event.button != wf::buttonbinding_t(button_preserve_aspect).get_button()))
        {
            return;
        }

        input_pressed(event.state);
    }

    void handle_pointer_motion(wf::pointf_t pointer_position, uint32_t time_ms) override
    {
        input_motion();
    }

    void handle_touch_up(uint32_t time_ms, int finger_id, wf::pointf_t lift_off_position) override
    {
        if (finger_id == 0)
        {
            input_pressed(WLR_BUTTON_RELEASED);
        }
    }

    void handle_touch_motion(uint32_t time_ms, int finger_id, wf::pointf_t position) override
    {
        if (finger_id == 0)
        {
            input_motion();
        }
    }

    void handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event ev) override
    {
        uint32_t key = ev.keycode;
        if (!is_using_keyboard)
        {
            return;
        }

        if (ev.state == WLR_KEY_PRESSED)
        {
            if (handle_key_pressed(key))
            {
                /* set up repeat if this key can be handled by us */
                current_key = key;
                key_repeat.set_callback(key, [=] (uint32_t key)
                {
                    handle_key_pressed(key);
                    return true;
                });
            }
        } else if (key == current_key)
        {
            key_repeat.disconnect();
            current_key = 0;
        }
    }

    /* Returns the currently used input coordinates in global compositor space */
    wf::point_t get_global_input_coords()
    {
        wf::pointf_t input;
        if (is_using_touch)
        {
            input = wf::get_core().get_touch_position(0);
        } else
        {
            input = wf::get_core().get_cursor_position();
        }

        return {(int)input.x, (int)input.y};
    }

    /* Returns the currently used input coordinates in output-local space */
    wf::point_t get_input_coords()
    {
        auto og = output->get_layout_geometry();

        return get_global_input_coords() - wf::point_t{og.x, og.y};
    }

    /* Calculate resize edges, grab starts at (sx, sy), view's geometry is vg */
    uint32_t calculate_edges(wf::geometry_t vg, int sx, int sy)
    {
        int view_x = sx - vg.x;
        int view_y = sy - vg.y;

        uint32_t edges = 0;
        if (view_x < vg.width / 2)
        {
            edges |= WLR_EDGE_LEFT;
        } else
        {
            edges |= WLR_EDGE_RIGHT;
        }

        if (view_y < vg.height / 2)
        {
            edges |= WLR_EDGE_TOP;
        } else
        {
            edges |= WLR_EDGE_BOTTOM;
        }

        return edges;
    }

    bool initiate(wayfire_toplevel_view view, uint32_t forced_edges = 0)
    {
        if (!view || (view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT) ||
            !view->is_mapped() || view->pending_fullscreen())
        {
            return false;
        }

        this->edges = forced_edges ?: calculate_edges(view->get_bounding_box(),
            get_input_coords().x, get_input_coords().y);

        if ((edges == 0) || !(view->get_allowed_actions() & wf::VIEW_ALLOW_RESIZE))
        {
            return false;
        }

        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        input_grab->set_wants_raw_input(true);
        input_grab->grab_input(wf::scene::layer::OVERLAY);

        key_diff   = {0, 0};
        last_input = grab_start = get_input_coords();
        grabbed_geometry = view->get_geometry();
        if (view->pending_tiled_edges())
        {
            view->toplevel()->pending().tiled_edges = 0;
        }

        this->view = view;

        auto og = view->get_bounding_box();
        int anchor_x = og.x;
        int anchor_y = og.y;

        if (edges & WLR_EDGE_LEFT)
        {
            anchor_x += og.width;
        }

        if (edges & WLR_EDGE_TOP)
        {
            anchor_y += og.height;
        }

        start_wobbly(view, anchor_x, anchor_y);
        wf::get_core().set_cursor(wlr_xcursor_get_resize_name((wlr_edges)edges));

        is_active = true;
        return true;
    }

    void input_pressed(uint32_t state)
    {
        if (state != WLR_BUTTON_RELEASED)
        {
            return;
        }

        deactivate();
    }

    void deactivate()
    {
        input_grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);
        is_active = false;

        if (view)
        {
            end_wobbly(view);

            wf::view_change_workspace_signal workspace_may_changed;
            workspace_may_changed.view = this->view;
            workspace_may_changed.to   = output->wset()->get_current_workspace();
            workspace_may_changed.old_workspace_valid = false;
            output->emit(&workspace_may_changed);
        }
    }

    // Convert resize edges to gravity
    uint32_t calculate_gravity()
    {
        uint32_t gravity = 0;
        if (edges & WLR_EDGE_LEFT)
        {
            gravity |= WLR_EDGE_RIGHT;
        }

        if (edges & WLR_EDGE_RIGHT)
        {
            gravity |= WLR_EDGE_LEFT;
        }

        if (edges & WLR_EDGE_TOP)
        {
            gravity |= WLR_EDGE_BOTTOM;
        }

        if (edges & WLR_EDGE_BOTTOM)
        {
            gravity |= WLR_EDGE_TOP;
        }

        return gravity;
    }

    void input_motion()
    {
        last_input = get_input_coords();
        update_size();
    }

    void update_size()
    {
        int dx = last_input.x - grab_start.x;
        int dy = last_input.y - grab_start.y;
        wf::geometry_t desired = grabbed_geometry;
        double ratio;
        if (preserve_aspect)
        {
            ratio = (double)desired.width / desired.height;
        }

        if (edges & WLR_EDGE_LEFT)
        {
            desired.x     += dx + key_diff.x;
            desired.width -= dx + key_diff.x;
        } else if (edges & WLR_EDGE_RIGHT)
        {
            desired.width += dx + key_diff.x;
        }

        if (edges & WLR_EDGE_TOP)
        {
            desired.y += dy + key_diff.y;
            desired.height -= dy + key_diff.y;
        } else if (edges & WLR_EDGE_BOTTOM)
        {
            desired.height += dy + key_diff.y;
        }

        if (preserve_aspect)
        {
            auto bbox = desired;
            desired.width  = std::min(std::max(bbox.width, 1), (int)(bbox.height * ratio));
            desired.height = std::min(std::max(bbox.height, 1), (int)(bbox.width / ratio));
            if (edges & WLR_EDGE_LEFT)
            {
                desired.x += bbox.width - desired.width;
            }

            if (edges & WLR_EDGE_TOP)
            {
                desired.y += bbox.height - desired.height;
            }
        } else
        {
            desired.width  = std::max(desired.width, 1);
            desired.height = std::max(desired.height, 1);
        }

        view->toplevel()->pending().gravity  = calculate_gravity();
        view->toplevel()->pending().geometry = desired;
        wf::get_core().tx_manager->schedule_object(view->toplevel());
    }

    /* Handle one key press. Returns whether the key press should be
     * repeated while it is held down. */
    bool handle_key_pressed(uint32_t key)
    {
        switch (key)
        {
          case KEY_UP:
            key_diff.y -= step;
            break;

          case KEY_DOWN:
            key_diff.y += step;
            break;

          case KEY_LEFT:
            key_diff.x -= step;
            break;

          case KEY_RIGHT:
            key_diff.x += step;
            break;

          case KEY_ENTER:
            deactivate();
            return false;

          default:
            return false;
        }

        update_size();
        return true;
    }

    void fini() override
    {
        if (input_grab->is_grabbed())
        {
            input_pressed(WLR_BUTTON_RELEASED);
        }

        output->rem_binding(&activate_binding);
        output->rem_binding(&activate_binding_preserve_aspect);
    }
};

class wayfire_resize_global : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<wayfire_resize>, public wf::ipc_activator_base_t
{
  public:

    void init() override
    {
        this->init_output_tracking();
        set_callbacks();
        load_from_xml_option("resize/activate_key");
    }

    void fini() override
    {
        unset_callbacks();
        this->fini_output_tracking();
    }

    void set_callbacks()
    {
        activator_cb = [=] (const wf::activator_data_t& data) -> bool
        {
            return output_instance[choose_output()]->activate_key_ipc(true, false, choose_view(data.source));
        };

        ipc_cb = [=] (const nlohmann::json& data)
        {
            WFJSON_OPTIONAL_FIELD(data, "allow_keyboard", boolean);
            WFJSON_OPTIONAL_FIELD(data, "preserve_aspect", boolean);

            wf::output_t *wo = wf::get_core().seat->get_active_output();
            if (!choose_ipc_output(data, &wo))
            {
                return wf::ipc::json_error("output id not found!");
            }

            wayfire_view view;
            if (!choose_ipc_view(data, &view))
            {
                return wf::ipc::json_error("view id not found!");
            }

            bool allow_keyboard  = true;
            bool preserve_aspect = false;

            if (data.contains("allow_keyboard"))
            {
                allow_keyboard = data["allow_keyboard"];
            }

            if (data.contains("preserve_aspect"))
            {
                preserve_aspect = data["preserve_aspect"];
            }

            output_instance[wo]->activate_key_ipc(allow_keyboard, preserve_aspect, view);

            return wf::ipc::json_ok();
        };
    }

    void unset_callbacks()
    {
        activator_cb = wf::activator_callback();
        ipc_cb = wf::ipc::method_callback();
    }
};


DECLARE_WAYFIRE_PLUGIN(wayfire_resize_global);

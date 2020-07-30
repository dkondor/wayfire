#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>

extern "C"
{
#include <wlr/types/wlr_workspace_v1.h>
}

namespace wf
{
/**
 * The workspaces manager is stored in core, to allow for a single instance.
 */
class wlr_workspaces_manager : public custom_data_t
{
  public:
    int refcount = 0;
    wlr_workspace_manager_v1 *manager;
    wlr_workspaces_manager()
    {
        manager = wlr_workspace_manager_v1_create(wf::get_core().display);
    }
};

class wlr_workspaces_intergration : public plugin_interface_t
{
  public:
    wlr_workspace_group_handle_v1 *group;
    std::vector<std::vector<wlr_workspace_handle_v1*>> workspaces;

    wf::wl_listener_wrapper on_commit;

    void init() override
    {
        /* Take ref to manager */
        auto manager = wf::get_core().get_data_safe<wlr_workspaces_manager>();
        ++manager->refcount;

        /* Create group & workspaces */
        group = wlr_workspace_group_handle_v1_create(manager->manager);

        dimensions_t ws_dim = output->workspace->get_workspace_grid_size();
        workspaces.resize(ws_dim.height,
            std::vector<wlr_workspace_handle_v1*>(ws_dim.width));
        for (int i = 0; i < ws_dim.height; i++)
        {
            for (int j = 0; j < ws_dim.width; j++)
            {
                workspaces[i][j] = wlr_workspace_handle_v1_create(group);

                std::string name =
                    output->to_string() + "workspace-" +
                    std::to_string(i * ws_dim.height + j);
                wlr_workspace_handle_v1_set_name(workspaces[i][j], name.c_str());

                wl_array coordinates;
                wl_array_init(&coordinates);
                *(int32_t*)wl_array_add(&coordinates, sizeof(int32_t)) = i;
                *(int32_t*)wl_array_add(&coordinates, sizeof(int32_t)) = j;
                wlr_workspace_handle_v1_set_coordinates(
                    workspaces[i][j], &coordinates);
                wl_array_release(&coordinates);
            }
        }

        /* Initially, workspace 0,0 is active */
        wlr_workspace_handle_v1_set_active(workspaces[0][0], true);
        output->connect_signal("workspace-changed", &on_current_workspace_changed);

        /* Listen for client requests */
        on_commit.set_callback([&] (void*)
        {
            point_t active_workspace = {0, 0};
            dimensions_t ws_dim = output->workspace->get_workspace_grid_size();

            for (int i = 0; i < ws_dim.height; i++)
            {
                for (int j = 0; j < ws_dim.width; j++)
                {
                    if (workspaces[i][j]->current &
                        WLR_WORKSPACE_HANDLE_V1_STATE_ACTIVE)
                    {
                        // workspaces in core are [column, row]
                        active_workspace = {j, i};
                    }
                }
            }

            output->workspace->request_workspace(active_workspace);
        });
    }

    signal_connection_t on_current_workspace_changed = [&] (signal_data_t *data)
    {
        auto ev = static_cast<wf::workspace_changed_signal*>(data);
        wlr_workspace_handle_v1_set_active(
            workspaces[ev->old_viewport.x][ev->old_viewport.y], false);
        wlr_workspace_handle_v1_set_active(
            workspaces[ev->new_viewport.x][ev->new_viewport.y], true);
    };

    void fini() override
    {
        auto manager = wf::get_core().get_data<wlr_workspaces_manager>();
        --manager->refcount;

        if (manager->refcount <= 0)
        {
            /** Make sure to clean up global data on shutdown */
            wf::get_core().erase_data<wlr_workspaces_manager>();
        }
    }

    /** Currently, we do not want to kill clients when unloading this plugin, so
     * we disallow disabling it. */
    bool is_unloadable() override
    {
        return false;
    }
};
}

DECLARE_WAYFIRE_PLUGIN(wf::wlr_workspaces_intergration);

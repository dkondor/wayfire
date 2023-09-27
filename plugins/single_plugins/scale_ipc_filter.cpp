#include <wayfire/core.hpp>
#include <wayfire/util.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/scale-signal.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <nlohmann/json.hpp>

class scale_ipc_activator : public wf::per_output_plugin_instance_t
{
    bool active = false;

    std::string current_filter; /* currently active filter */
    bool current_case_sensitive = true; /* whether the filter should be case sensitive */

    bool should_show_view(wayfire_toplevel_view view) const
    {
        const std::string& app_id_str = current_filter;
        if (app_id_str.empty())
        {
            return true;
        }

        if (!current_case_sensitive)
        {
            std::string app_id = view->get_app_id();
            std::transform(app_id.begin(), app_id.end(), app_id.begin(),
                [] (unsigned char c)
            {
                return (char)std::tolower(c);
            });
            return app_id == app_id_str;
        } else
        {
            return view->get_app_id() == app_id_str;
        }
    }

  public:
    void init() override
    {
        output->connect(&view_filter);
        output->connect(&scale_end);
    }

    void fini() override
    {
        view_filter.disconnect();
        scale_end.disconnect();
    }

    nlohmann::json activate(const std::string& filter, bool case_sensitive, bool all_workspaces)
    {
        current_filter = filter;
        current_case_sensitive = case_sensitive;
        active = true;
        if (!output->is_plugin_active("scale"))
        {
            nlohmann::json data;
            data["output_id"] = output->get_id();
            wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> repo;
            return repo->call_method(all_workspaces ? "scale/toggle_all" : "scale/toggle", data);
        } else
        {
            scale_update_signal signal;
            output->emit(&signal);
            return wf::ipc::json_ok();
        }
    }

    wf::signal::connection_t<scale_filter_signal> view_filter = [this] (scale_filter_signal *signal)
    {
        if (active)
        {
            scale_filter_views(signal, [this] (wayfire_toplevel_view v)
            {
                return !should_show_view(v);
            });
        }
    };

    wf::signal::connection_t<scale_end_signal> scale_end = [this] (scale_end_signal *data)
    {
        active = false;
    };
};

class scale_ipc_activator_global : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<scale_ipc_activator>
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> method_repository;

  public:
    void init() override
    {
        this->init_output_tracking();
        method_repository->register_method("scale/activate_appid", activate);
    }

    void fini() override
    {
        method_repository->unregister_method("scale/activate_appid");
        this->fini_output_tracking();
    }

    wf::ipc::method_callback activate = [=] (nlohmann::json data)
    {
        bool case_sensitive  = true;
        bool all_workspaces  = false;
        wf::output_t *output = nullptr;

        WFJSON_EXPECT_FIELD(data, "app_id", string);
        WFJSON_OPTIONAL_FIELD(data, "case_sensitive", boolean);
        WFJSON_OPTIONAL_FIELD(data, "all_workspaces", boolean);
        WFJSON_OPTIONAL_FIELD(data, "output_id", number_integer);

        if (data.count("case_sensitive"))
        {
            case_sensitive = data["case_sensitive"];
        }

        if (data.count("all_workspaces"))
        {
            all_workspaces = data["all_workspaces"];
        }

        if (data.count("output_id"))
        {
            output = wf::ipc::find_output_by_id(data["id"]);
            if (!output)
            {
                return wf::ipc::json_error("output not found");
            }
        } else
        {
            output = wf::get_core().get_active_output();
            if (!output)
            {
                return wf::ipc::json_error("no active output");
            }
        }

        return this->output_instance[output]->activate(data["app_id"], case_sensitive, all_workspaces);
    };
};

DECLARE_WAYFIRE_PLUGIN(scale_ipc_activator_global)

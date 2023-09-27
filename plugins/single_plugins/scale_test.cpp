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
#include <nlohmann/json.hpp>

class wayfire_scale_test : public wf::per_output_plugin_instance_t
{
    wf::option_wrapper_t<std::string> app_id_filter{"scale_test/app_id"};
    wf::option_wrapper_t<bool> case_sensitive{"scale_test/case_sensitive"};
    wf::option_wrapper_t<bool> all_workspaces{"scale_test/all_workspaces"};
    bool active = false;

    bool should_show_view(wayfire_toplevel_view view) const
    {
        const std::string& app_id_str = app_id_filter;
        if (app_id_str.empty())
        {
            return true;
        }

        if (!case_sensitive)
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
        
        output->add_key(wf::option_wrapper_t<wf::keybinding_t>{"scale_test/activate"}, &activate);
    }

    void fini() override
    {
        output->rem_binding(&activate);
        view_filter.disconnect();
        scale_end.disconnect();
    }

    wf::key_callback activate = [=] (auto)
    {
        active = true;
        if (!output->is_plugin_active("scale"))
        {
            nlohmann::json data;
			data["output_id"] = output->get_id();
			wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> repo;
			repo->call_method(all_workspaces ? "scale/toggle_all" : "scale/toggle", data);
        } else
        {
			scale_update_signal signal;
            output->emit(&signal);
        }
        return true;
    };

    wf::signal::connection_t<scale_filter_signal> view_filter = [this] (scale_filter_signal* signal)
        {
            if (active)
            {
                scale_filter_views(signal, [this] (wayfire_toplevel_view v)
                {
                    return !should_show_view(v);
                });
            }
        };

    wf::signal::connection_t<scale_end_signal> scale_end = [this] (scale_end_signal* data)
        {
            active = false;
        };
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_scale_test>)

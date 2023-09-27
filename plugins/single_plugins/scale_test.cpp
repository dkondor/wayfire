#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugins/scale-signal.hpp>

class wayfire_scale_test : public wf::plugin_interface_t
{
    wf::option_wrapper_t<std::string> app_id_filter{"scale_test/app_id"};
    wf::option_wrapper_t<bool> case_sensitive{"scale_test/case_sensitive"};
    wf::option_wrapper_t<bool> all_workspaces{"scale_test/all_workspaces"};
    bool active = false;

    bool should_show_view(wayfire_view view) const
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
        grab_interface->name = "scale_test";
        grab_interface->capabilities = 0;

        output->connect_signal("scale-filter", &view_filter);
        output->connect_signal("scale-end", &scale_end);

        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"scale_test/activate"},
            &activate);
    }

    void fini() override
    {
        output->rem_binding(&activate);
        output->disconnect_signal(&view_filter);
        output->disconnect_signal(&scale_end);
    }

    wf::activator_callback activate = [=] (auto)
    {
        active = true;
        if (!output->is_plugin_active("scale"))
        {
            wf::activator_data_t data;
            data.source = wf::activator_source_t::PLUGIN;
            return output->call_plugin(all_workspaces ? "scale/toggle_all" : "scale/toggle", data);
        } else
        {
            output->emit_signal("scale-update", nullptr);
        }
        return true;
    };

    wf::signal_connection_t view_filter{[this] (wf::signal_data_t *data)
        {
            if (active)
            {
                auto signal = static_cast<scale_filter_signal*>(data);
                scale_filter_views(signal, [this] (wayfire_view v)
                {
                    return !should_show_view(v);
                });
            }
        }
    };

    wf::signal_connection_t scale_end{[this] (wf::signal_data_t *data)
        {
            active = false;
        }
    };
};

DECLARE_WAYFIRE_PLUGIN(wayfire_scale_test);

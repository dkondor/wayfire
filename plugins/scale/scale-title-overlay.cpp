#include "scale-title-overlay.hpp"

#include <wayfire/opengl.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <wayfire/plugins/common/simple-texture.hpp>

/**
 * Class storing an overlay with a view's title, added to scale's transformer.
 */
class view_title_overlay
{
  protected:
    wf::cairo_text_t overlay;
    wf::cairo_text_t::params par;
    /* the transformer we are attached to */
    wf::scale_transformer& tr;
    /* if text potentially overflows the available space */
    bool overflow = false;

    /**
     * Get the transformed bounding box of the view, including the
     * current transform
     */
    wlr_box get_transformed_bounding_box()
    {
        wlr_box box = tr.get_transformed_view()->get_wm_geometry();
        box = tr.trasform_box_without_padding(box);
        return box;
    }

    void render_overlay(const wlr_box& box)
    {
        par.max_size = {box.width, box.height};
        auto view = tr.get_transformed_view();
        auto res  = overlay.render_text(view->get_title(), par);
        overflow = res.width > overlay.tex.width;
        view->damage();
    }

    void render_overlay()
    {
        render_overlay(get_transformed_bounding_box());
    }

  public:
    enum class position
    {
        TOP,
        CENTER,
        BOTTOM,
    };

    position pos = position::CENTER;

    GLuint get_texture(float output_scale, wf::geometry_t& geometry)
    {
        wlr_box box = get_transformed_bounding_box();

        /**
         * regenerate the overlay texture in the following cases:
         * 1. Output's scale changed
         * 2. The overlay does not fit anymore
         * 3. The overlay previously did not fit, but there is more space now
         * TODO: check if this wastes too high CPU power when views are being
         * animated and maybe redraw less frequently or in a way that will not
         * hold up the rendering loop!
         */
        if ((output_scale != par.output_scale) ||
            (overlay.tex.width > box.width * output_scale) ||
            (overflow && (overlay.tex.width < std::floor(box.width * output_scale))))
        {
            par.output_scale = output_scale;
            render_overlay(box);
        }

        int w = overlay.tex.width;
        int h = overlay.tex.height;
        int y = 0;
        switch (pos)
        {
          /* TODO: Add some padding! */
          case position::TOP:
            y = box.y - (int)(h / output_scale);
            break;

          case position::CENTER:
            y = box.y + box.height / 2 - (int)(h / output_scale / 2);
            break;

          case position::BOTTOM:
            y = box.y + box.height;
            break;
        }

        geometry = {box.x + box.width / 2 - (int)(w / output_scale / 2),
            y, (int)(w / output_scale), (int)(h / output_scale)};
        return overlay.tex.tex;
    }

    view_title_overlay(wf::scale_transformer& tr_, int font_size,
        const wf::color_t& bg_color, const wf::color_t& text_color) : tr(tr_)
    {
        par.font_size  = font_size;
        par.bg_color   = bg_color;
        par.text_color = text_color;
        par.exact_size = true;
        render_overlay();
        tr.get_transformed_view()->connect_signal("title-changed", &view_changed);
    }

    wf::signal_connection_t view_changed = [this] (auto)
    {
        render_overlay();
    };

    wf::dimensions_t get_size() const
    {
        return {overlay.tex.width, overlay.tex.height};
    }

    void render(const wf::framebuffer_t& fb, const wf::region_t& damage)
    {
        wf::geometry_t geometry;
        GLuint tex = get_texture(fb.scale, geometry);

        if (tex == (GLuint) - 1)
        {
            return;
        }

        auto ortho = fb.get_orthographic_projection();
        OpenGL::render_begin(fb);
        for (const auto& box : damage)
        {
            fb.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::render_transformed_texture(tex, geometry, ortho,
                {1.0f, 1.0f, 1.0f, tr.alpha}, OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
        }

        OpenGL::render_end();
    }
};

scale_show_title_t::scale_show_title_t() :
    view_filter{[this] (auto)
    {
        update_title_overlay_opt();
    }},

    scale_end{[this] (wf::signal_data_t*)
    {
        show_view_title_overlay = title_overlay_t::NEVER;
        last_title_overlay = nullptr;
        mouse_update.disconnect();
    }
},

add_title_overlay{[this] (wf::signal_data_t *data)
    {
        const std::string& opt = show_view_title_overlay_opt;
        if (opt == "never")
        {
            /* TODO: support changing this option while scale is running! */
            return;
        }

        auto signal = static_cast<scale_transformer_added_signal*>(data);
        auto tr     = signal->transformer;
        auto view   = tr->get_transformed_view();
        auto ol     = std::make_shared<view_title_overlay>(*tr, title_font_size,
            bg_color, text_color);
        auto cb = std::make_shared<wf::scale_view_overlay>([ol, this, view] (
            const wf::framebuffer_t& fb,
            const wf::region_t& damage)
        {
            auto tmp_view = view;
            while (tmp_view->parent)
            {
                tmp_view = tmp_view->parent;
            }

            if ((show_view_title_overlay == title_overlay_t::NEVER) ||
                ((show_view_title_overlay == title_overlay_t::MOUSE) &&
                 (tmp_view != last_title_overlay)))
            {
                return;
            }

            ol->render(fb, damage);
        });

        const std::string& pos = title_position;
        if (pos == "top")
        {
            ol->pos = view_title_overlay::position::TOP;
        } else if (pos == "bottom")
        {
            ol->pos = view_title_overlay::position::BOTTOM;
        }

        tr->add_overlay(cb, 1);
    }
},

mouse_update{[this] (auto)
    {
        update_title_overlay_mouse();
    }
},

scale_padding{[this] (wf::signal_data_t *data)
    {
        const std::string& pos = title_position;
        if (pos == "center")
        {
            return;
        }

        auto signal = static_cast<scale_padding_signal*>(data);
        unsigned int extra_pad = wf::cairo_text_t::measure_height(
            title_font_size, true);
        if (pos == "top")
        {
            signal->pad.top = std::max(signal->pad.top, extra_pad);
        } else if (pos == "bottom")
        {
            signal->pad.bottom = std::max(signal->pad.bottom, extra_pad);
        }
    }
}

{}

void scale_show_title_t::init(wf::output_t *output)
{
    this->output = output;
    output->connect_signal("scale-filter", &view_filter);
    output->connect_signal("scale-padding", &scale_padding);
    output->connect_signal("scale-transformer-added", &add_title_overlay);
    output->connect_signal("scale-end", &scale_end);
}

void scale_show_title_t::fini()
{
    output->disconnect_signal(&view_filter);
    output->disconnect_signal(&add_title_overlay);
    output->disconnect_signal(&scale_end);
    mouse_update.disconnect();
}

void scale_show_title_t::update_title_overlay_opt()
{
    const std::string& tmp = show_view_title_overlay_opt;
    if (tmp == "all")
    {
        show_view_title_overlay = title_overlay_t::ALL;
    } else if (tmp == "mouse")
    {
        show_view_title_overlay = title_overlay_t::MOUSE;
    } else
    {
        show_view_title_overlay = title_overlay_t::NEVER;
    }

    if (show_view_title_overlay == title_overlay_t::MOUSE)
    {
        update_title_overlay_mouse();
        mouse_update.disconnect();
        wf::get_core().connect_signal("pointer_motion_post", &mouse_update);
    }
}

void scale_show_title_t::update_title_overlay_mouse()
{
    wayfire_view v;

    wf::option_wrapper_t<bool> interact{"scale/interact"};

    if (interact)
    {
        /* we can use normal focus tracking */
        v = wf::get_core().get_cursor_focus_view();
    } else
    {
        auto& core = wf::get_core();
        v = core.get_view_at(core.get_cursor_position());
    }

    if (v)
    {
        while (v->parent)
        {
            v = v->parent;
        }

        if (v->role != wf::VIEW_ROLE_TOPLEVEL)
        {
            v = nullptr;
        }
    }

    if (v != last_title_overlay)
    {
        if (last_title_overlay)
        {
            last_title_overlay->damage();
        }

        last_title_overlay = v;
        if (v)
        {
            v->damage();
        }
    }
}

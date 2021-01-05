#pragma once

#include <wayfire/view-transform.hpp>
#include <wayfire/nonstd/observer_ptr.h>
#include <string>
#include <list>
#include <algorithm>

namespace wf
{
/**
 * transformer used by scale -- it is an extension of the 2D transformer
 * with the ability to add overlays
 */

class scale_transformer;
/**
 * Overlay function. It is called after the transformed view was already
 * rendered.
 *
 * @param fb     The framebuffer to render to.
 * @param damage Damaged region to render.
 * @param tr     Reference to the transformer calling this function.
 *               It is not marked const to be able to call the transform
 *               functions, but this function shuld not modify the
 *               transform parameters (e.g. scale or alpha).
 * @param data   Any custom data stored when adding this overlay.
 */
using scale_view_overlay = std::function<void (const wf::framebuffer_t& fb,
    const wf::region_t& damage)>;

class scale_transformer : public wf::view_2D
{
  public:
    scale_transformer(wayfire_view view) : wf::view_2D(view)
    {}
    ~scale_transformer()
    {}

    struct padding
    {
        unsigned int top    = 0;
        unsigned int left   = 0;
        unsigned int bottom = 0;
        unsigned int right  = 0;
    };

    uint32_t get_z_order() override
    {
        return wf::TRANSFORMER_HIGHLEVEL + 1;
    }

    /* render the transformed view and then add all overlays */
    void render_with_damage(wf::texture_t src_tex, wlr_box src_box,
        const wf::region_t& damage, const wf::framebuffer_t& target_fb) override
    {
        /* render the transformed view first */
        view_transformer_t::render_with_damage(src_tex, src_box, damage, target_fb);

        /* call all transformers */
        for (auto& ol : overlays)
        {
            (*ol->ol)(target_fb, damage);
        }
    }

    /**
     * Add a new overlay that is rendered after this transform.
     *
     * @param ol      The callback function that does the rendering.
     * @param z_order Relative order; overlays are called in order.
     * @param data    Any custom data to store. It is passed to the
     *                callback function and is automatically destroyed
     *                when the overlay is removed.
     * @param pad     Any extra padding to use. Paddings are not cumulative,
     *                i.e. if two overlays add a top padding, the larger
     *                of the two values is used.
     */
    void add_overlay(std::shared_ptr<scale_view_overlay>& ol, int z_order,
        const padding& pad = {0, 0, 0, 0})
    {
        auto it =
            std::find_if(overlays.begin(), overlays.end(),
                [z_order] (const auto& other)
        {
            return other->z_order >= z_order;
        });

        overlays.insert(it, std::make_unique<overlay>(ol, z_order, pad));
        extend_padding(this->pad, pad);
        view->damage();
    }

    void add_overlay(std::shared_ptr<scale_view_overlay>&& ol, int z_order,
        const padding& pad = {0, 0, 0, 0})
    {
        add_overlay(ol, z_order, pad);
    }

    /* remove an existing overlay */
    void rem_overlay(scale_view_overlay *ol)
    {
        view->damage();
        overlays.remove_if([ol] (const auto& other)
        {
            return other->ol.get() == ol;
        });

        /* recalculate padding */
        pad = {0, 0, 0, 0};
        for (const auto& ol : overlays)
        {
            extend_padding(pad, ol->pad);
        }

        view->damage();
    }

    /* get the view being transformed (it is protected in view_2D) */
    wayfire_view get_transformed_view() const
    {
        return view;
    }

    /**
     * Transformer name used by scale. This can be used by other plugins to find
     * scale's transformer on a view.
     */
    static std::string transformer_name()
    {
        return "scale";
    }

    /**
     * Transform a box, including the current transform, but not the padding.
     */
    wlr_box trasform_box_without_padding(wlr_box box)
    {
        box = view->transform_region(box, this);
        wlr_box view_box = view->get_bounding_box(this);
        return view_transformer_t::get_bounding_box(view_box, box);
    }

    /**
     * Transform a region and add padding to it.
     * Note: this will pad any transformed region, not only if it corresponds to
     * the view's bounding box.
     */
    wlr_box get_bounding_box(wf::geometry_t view, wlr_box region) override
    {
        region    = view_transformer_t::get_bounding_box(view, region);
        region.x -= pad.left;
        region.y -= pad.top;
        region.width  += pad.left + pad.right;
        region.height += pad.top + pad.bottom;
        return region;
    }

    const padding& get_padding() const
    {
        return pad;
    }

    /**
     * extend padding in target to ensure it is at least as large as other
     */
    static void extend_padding(padding& target, const padding& other)
    {
        target.top    = std::max(target.top, other.top);
        target.left   = std::max(target.left, other.left);
        target.bottom = std::max(target.bottom, other.bottom);
        target.right  = std::max(target.right, other.right);
    }

  protected:
    /* extra info stored together with the overlay callbacks */
    struct overlay
    {
        std::shared_ptr<scale_view_overlay> ol; /* callback function */
        int z_order; /* order in which overalys are applied */
        padding pad; /* extra padding needed by this overlay */
        overlay(std::shared_ptr<scale_view_overlay>& ol_, int z_order_,
            const padding& pad_) :
            ol(ol_), z_order(z_order_), pad(pad_)
        {}
    };

    std::list<std::unique_ptr<overlay>> overlays; /* list of active overlays */
    padding pad; /* combined padding used */
};
}

// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage


#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/timer.h>

#include <OSL/hashes.h>
#include <OSL/oslexec.h>

#include "osltoyrenderer.h"

// Create ustrings for all strings used by the free function renderer services.
// Required to allow the reverse mapping of hash->string to work when processing messages
namespace RS {
namespace Strings {
#define RS_STRDECL(str, var_name) const OSL::ustring var_name { str };
#include "rs_strdecls.h"
#undef RS_STRDECL
}  // namespace Strings
}  // namespace RS

namespace RS {
namespace {
namespace Hashes {
#define RS_STRDECL(str, var_name) \
    constexpr OSL::ustringhash var_name(OSL::strhash(str));
#include "rs_strdecls.h"
#undef RS_STRDECL
};  //namespace Hashes
}  // unnamed namespace
};  //namespace RS

using namespace OSL;



OSL_NAMESPACE_BEGIN

static constexpr TypeDesc TypeFloatArray2(TypeDesc::FLOAT, 2);
static constexpr TypeDesc TypeFloatArray4(TypeDesc::FLOAT, 4);
static constexpr TypeDesc TypeIntArray2(TypeDesc::INT, 2);



OSLToyRenderer::OSLToyRenderer()
{
    m_shadingsys = new ShadingSystem(this);
    m_shadingsys->attribute("allow_shader_replacement", 1);
    ustring outputs[] = { ustring("Cout") };
    m_shadingsys->attribute("renderer_outputs", TypeDesc(TypeDesc::STRING, 1),
                            &outputs);
    // set attributes for the shadingsys

    Matrix44 M;
    M.makeIdentity();
    camera_params(M, RS::Hashes::perspective, 90.0f, 0.1f, 1000.0f, 256, 256);

    // Set up getters
    m_attr_getters[RS::Hashes::osl_version] = &OSLToyRenderer::get_osl_version;
    m_attr_getters[RS::Hashes::camera_resolution]
        = &OSLToyRenderer::get_camera_resolution;
    m_attr_getters[RS::Hashes::camera_projection]
        = &OSLToyRenderer::get_camera_projection;
    m_attr_getters[RS::Hashes::camera_pixelaspect]
        = &OSLToyRenderer::get_camera_pixelaspect;
    m_attr_getters[RS::Hashes::camera_screen_window]
        = &OSLToyRenderer::get_camera_screen_window;
    m_attr_getters[RS::Hashes::camera_fov]  = &OSLToyRenderer::get_camera_fov;
    m_attr_getters[RS::Hashes::camera_clip] = &OSLToyRenderer::get_camera_clip;
    m_attr_getters[RS::Hashes::camera_clip_near]
        = &OSLToyRenderer::get_camera_clip_near;
    m_attr_getters[RS::Hashes::camera_clip_far]
        = &OSLToyRenderer::get_camera_clip_far;
    m_attr_getters[RS::Hashes::camera_shutter]
        = &OSLToyRenderer::get_camera_shutter;
    m_attr_getters[RS::Hashes::camera_shutter_open]
        = &OSLToyRenderer::get_camera_shutter_open;
    m_attr_getters[RS::Hashes::camera_shutter_close]
        = &OSLToyRenderer::get_camera_shutter_close;

    // Set up default shaderglobals
    ShaderGlobals& sg(m_shaderglobals_template);
    memset((char*)&sg, 0, sizeof(ShaderGlobals));
    Matrix44 Mshad, Mobj;  // just let these be identity for now
    // Set "shader" space to be Mshad.  In a real renderer, this may be
    // different for each shader group.
    sg.shader2common = OSL::TransformationPtr(&Mshad);
    // Set "object" space to be Mobj.  In a real renderer, this may be
    // different for each object.
    sg.object2common = OSL::TransformationPtr(&Mobj);
    // Just make it look like all shades are the result of 'raytype' rays.
    sg.raytype = 0;  // default ray type
    // Set the surface area of the patch to 1 (which it is).  This is
    // only used for light shaders that call the surfacearea() function.
    sg.surfacearea = 1;
    // Derivs are constant across the image
    // if (shadelocations == ShadePixelCenters) {
    sg.dudx = 1.0f / m_xres;  // sg.dudy is already 0
    sg.dvdy = 1.0f / m_yres;  // sg.dvdx is already 0
    // } else {
    //     sg.dudx  = 1.0f / std::max(1,(m_xres-1));
    //     sg.dvdy  = 1.0f / std::max(1,(m_yres-1));
    // }
    // Derivatives with respect to x,y
    sg.dPdx = Vec3(1.0f, 0.0f, 0.0f);
    sg.dPdy = Vec3(0.0f, 1.0f, 0.0f);
    sg.dPdz = Vec3(0.0f, 0.0f, 1.0f);
    // Tangents of P with respect to surface u,v
    sg.dPdu = Vec3(m_xres, 0.0f, 0.0f);
    sg.dPdv = Vec3(0.0f, m_yres, 0.0f);
    sg.dPdz = Vec3(0.0f, 0.0f, 0);
    // That also implies that our normal points to (0,0,1)
    sg.N  = Vec3(0, 0, 1);
    sg.Ng = Vec3(0, 0, 1);
    // In our SimpleRenderer, the "renderstate" itself just a pointer to
    // the ShaderGlobals.
    // sg.renderstate = &sg;
}



void
OSLToyRenderer::render_image()
{
    if (!m_framebuffer.initialized())
        m_framebuffer.reset(
            OIIO::ImageSpec(m_xres, m_yres, 3, TypeDesc::FLOAT));

    static ustring outputs[] = { ustring("Cout") };
    OIIO::paropt popt(0, OIIO::paropt::SplitDir::Tile, 4096);
    shade_image(*shadingsys(), *shadergroup(), &m_shaderglobals_template,
                m_framebuffer, outputs, ShadePixelCenters, OIIO::ROI(), popt);
    //    std::cout << timer() << "\n";
}



int
OSLToyRenderer::supports(string_view /*feature*/) const
{
    return false;
}



void
OSLToyRenderer::camera_params(const Matrix44& world_to_camera,
                              ustringhash projection, float hfov, float hither,
                              float yon, int xres, int yres)
{
    m_world_to_camera  = world_to_camera;
    m_projection       = projection;
    m_fov              = hfov;
    m_pixelaspect      = 1.0f;  // hard-coded
    m_hither           = hither;
    m_yon              = yon;
    m_shutter[0]       = 0.0f;
    m_shutter[1]       = 1.0f;  // hard-coded
    float frame_aspect = float(xres) / float(yres) * m_pixelaspect;
    m_screen_window[0] = -frame_aspect;
    m_screen_window[1] = -1.0f;
    m_screen_window[2] = frame_aspect;
    m_screen_window[3] = 1.0f;
    m_xres             = xres;
    m_yres             = yres;
}



bool
OSLToyRenderer::get_matrix(ShaderGlobals* /*sg*/, Matrix44& result,
                           TransformationPtr xform, float /*time*/)
{
    // OSLToyRenderer doesn't understand motion blur and transformations
    // are just simple 4x4 matrices.
    result = *reinterpret_cast<const Matrix44*>(xform);
    return true;
}



bool
OSLToyRenderer::get_matrix(ShaderGlobals* /*sg*/, Matrix44& result,
                           ustringhash from, float /*time*/)
{
    TransformMap::const_iterator found = m_named_xforms.find(from);
    if (found != m_named_xforms.end()) {
        result = *(found->second);
        return true;
    } else {
        return false;
    }
}



bool
OSLToyRenderer::get_matrix(ShaderGlobals* /*sg*/, Matrix44& result,
                           TransformationPtr xform)
{
    // OSLToyRenderer doesn't understand motion blur and transformations
    // are just simple 4x4 matrices.
    result = *(OSL::Matrix44*)xform;
    return true;
}



bool
OSLToyRenderer::get_matrix(ShaderGlobals* /*sg*/, Matrix44& result,
                           ustringhash from)
{
    // OSLToyRenderer doesn't understand motion blur, so we never fail
    // on account of time-varying transformations.
    TransformMap::const_iterator found = m_named_xforms.find(from);
    if (found != m_named_xforms.end()) {
        result = *(found->second);
        return true;
    } else {
        return false;
    }
}



bool
OSLToyRenderer::get_inverse_matrix(ShaderGlobals* /*sg*/, Matrix44& result,
                                   ustringhash to, float /*time*/)
{
    if (to == OSL::Hashes::camera || to == OSL::Hashes::screen
        || to == OSL::Hashes::NDC || to == RS::Hashes::raster) {
        Matrix44 M = m_world_to_camera;
        if (to == OSL::Hashes::screen || to == OSL::Hashes::NDC
            || to == RS::Hashes::raster) {
            float depthrange = (double)m_yon - (double)m_hither;
            if (m_projection == RS::Hashes::perspective) {
                float tanhalffov = tanf(0.5f * m_fov * M_PI / 180.0);
                Matrix44 camera_to_screen(1 / tanhalffov, 0, 0, 0, 0,
                                          1 / tanhalffov, 0, 0, 0, 0,
                                          m_yon / depthrange, 1, 0, 0,
                                          -m_yon * m_hither / depthrange, 0);
                M = M * camera_to_screen;
            } else {
                Matrix44 camera_to_screen(1, 0, 0, 0, 0, 1, 0, 0, 0, 0,
                                          1 / depthrange, 0, 0, 0,
                                          -m_hither / depthrange, 1);
                M = M * camera_to_screen;
            }
            if (to == OSL::Hashes::NDC || to == RS::Hashes::raster) {
                float screenleft = -1.0, screenwidth = 2.0;
                float screenbottom = -1.0, screenheight = 2.0;
                Matrix44 screen_to_ndc(1 / screenwidth, 0, 0, 0, 0,
                                       1 / screenheight, 0, 0, 0, 0, 1, 0,
                                       -screenleft / screenwidth,
                                       -screenbottom / screenheight, 0, 1);
                M = M * screen_to_ndc;
                if (to == RS::Hashes::raster) {
                    Matrix44 ndc_to_raster(m_xres, 0, 0, 0, 0, m_yres, 0, 0, 0,
                                           0, 1, 0, 0, 0, 0, 1);
                    M = M * ndc_to_raster;
                }
            }
        }
        result = M;
        return true;
    }

    TransformMap::const_iterator found = m_named_xforms.find(to);
    if (found != m_named_xforms.end()) {
        result = *(found->second);
        result.invert();
        return true;
    } else {
        return false;
    }
}



void
OSLToyRenderer::name_transform(const char* name, const OSL::Matrix44& xform)
{
    std::shared_ptr<Transformation> M(new OSL::Matrix44(xform));
    m_named_xforms[ustringhash(name)] = M;
}



bool
OSLToyRenderer::get_array_attribute(ShaderGlobals* sg, bool derivatives,
                                    ustringhash object, TypeDesc type,
                                    ustringhash name, int index, void* val)
{
    AttrGetterMap::const_iterator g = m_attr_getters.find(name);
    if (g != m_attr_getters.end()) {
        AttrGetter getter = g->second;
        return (this->*(getter))(sg, derivatives, object, type, name, val);
    }

    if (object == RS::Hashes::mouse) {
        if (name == RS::Hashes::s && type == TypeDesc::FLOAT
            && m_mouse_x >= 0) {
            *(float*)val = (m_mouse_x + 0.5f) / float(m_xres);
            return true;
        }
        if (name == RS::Hashes::t && type == TypeDesc::FLOAT
            && m_mouse_y >= 0) {
            *(float*)val = (m_mouse_y + 0.5f) / float(m_yres);
            return true;
        }
    }

    // In order to test getattribute(), respond positively to
    // "options"/"blahblah"
    if (object == RS::Hashes::options && name == RS::Hashes::blahblah
        && type == TypeFloat) {
        *(float*)val = 3.14159;
        return true;
    }

    // If no named attribute was found, allow userdata to bind to the
    // attribute request.
    if (object.empty() && index == -1)
        return get_userdata(derivatives, name, type, sg, val);

    return false;
}



bool
OSLToyRenderer::get_attribute(ShaderGlobals* sg, bool derivatives,
                              ustringhash object, TypeDesc type,
                              ustringhash name, void* val)
{
    return get_array_attribute(sg, derivatives, object, type, name, -1, val);
}



bool
OSLToyRenderer::get_userdata(bool derivatives, ustringhash name, TypeDesc type,
                             ShaderGlobals* sg, void* val)
{
    // Just to illustrate how this works, respect s and t userdata, filled
    // in with the uv coordinates.  In a real renderer, it would probably
    // look up something specific to the primitive, rather than have hard-
    // coded names.

    if (name == RS::Hashes::s && type == TypeFloat) {
        ((float*)val)[0] = sg->u;
        if (derivatives) {
            ((float*)val)[1] = sg->dudx;
            ((float*)val)[2] = sg->dudy;
        }
        return true;
    }
    if (name == RS::Hashes::t && type == TypeFloat) {
        ((float*)val)[0] = sg->v;
        if (derivatives) {
            ((float*)val)[1] = sg->dvdx;
            ((float*)val)[2] = sg->dvdy;
        }
        return true;
    }

    return false;
}


bool
OSLToyRenderer::get_osl_version(ShaderGlobals* /*sg*/, bool /*derivs*/,
                                ustringhash /*object*/, TypeDesc type,
                                ustringhash /*name*/, void* val)
{
    if (type == TypeInt) {
        ((int*)val)[0] = OSL_VERSION;
        return true;
    }
    return false;
}


bool
OSLToyRenderer::get_camera_resolution(ShaderGlobals* /*sg*/, bool /*derivs*/,
                                      ustringhash /*object*/, TypeDesc type,
                                      ustringhash /*name*/, void* val)
{
    if (type == TypeIntArray2) {
        ((int*)val)[0] = m_xres;
        ((int*)val)[1] = m_yres;
        return true;
    }
    return false;
}


bool
OSLToyRenderer::get_camera_projection(ShaderGlobals* /*sg*/, bool /*derivs*/,
                                      ustringhash /*object*/, TypeDesc type,
                                      ustringhash /*name*/, void* val)
{
    if (type == TypeString) {
        ((ustringhash*)val)[0] = m_projection;
        return true;
    }
    return false;
}


bool
OSLToyRenderer::get_camera_fov(ShaderGlobals* /*sg*/, bool derivs,
                               ustringhash /*object*/, TypeDesc type,
                               ustringhash /*name*/, void* val)
{
    // N.B. in a real renderer, this may be time-dependent
    if (type == TypeFloat) {
        ((float*)val)[0] = m_fov;
        if (derivs)
            memset((char*)val + type.size(), 0, 2 * type.size());
        return true;
    }
    return false;
}


bool
OSLToyRenderer::get_camera_pixelaspect(ShaderGlobals* /*sg*/, bool derivs,
                                       ustringhash /*object*/, TypeDesc type,
                                       ustringhash /*name*/, void* val)
{
    if (type == TypeFloat) {
        ((float*)val)[0] = m_pixelaspect;
        if (derivs)
            memset((char*)val + type.size(), 0, 2 * type.size());
        return true;
    }
    return false;
}


bool
OSLToyRenderer::get_camera_clip(ShaderGlobals* /*sg*/, bool derivs,
                                ustringhash /*object*/, TypeDesc type,
                                ustringhash /*name*/, void* val)
{
    if (type == TypeFloatArray2) {
        ((float*)val)[0] = m_hither;
        ((float*)val)[1] = m_yon;
        if (derivs)
            memset((char*)val + type.size(), 0, 2 * type.size());
        return true;
    }
    return false;
}


bool
OSLToyRenderer::get_camera_clip_near(ShaderGlobals* /*sg*/, bool derivs,
                                     ustringhash /*object*/, TypeDesc type,
                                     ustringhash /*name*/, void* val)
{
    if (type == TypeFloat) {
        ((float*)val)[0] = m_hither;
        if (derivs)
            memset((char*)val + type.size(), 0, 2 * type.size());
        return true;
    }
    return false;
}


bool
OSLToyRenderer::get_camera_clip_far(ShaderGlobals* /*sg*/, bool derivs,
                                    ustringhash /*object*/, TypeDesc type,
                                    ustringhash /*name*/, void* val)
{
    if (type == TypeFloat) {
        ((float*)val)[0] = m_yon;
        if (derivs)
            memset((char*)val + type.size(), 0, 2 * type.size());
        return true;
    }
    return false;
}



bool
OSLToyRenderer::get_camera_shutter(ShaderGlobals* /*sg*/, bool derivs,
                                   ustringhash /*object*/, TypeDesc type,
                                   ustringhash /*name*/, void* val)
{
    if (type == TypeFloatArray2) {
        ((float*)val)[0] = m_shutter[0];
        ((float*)val)[1] = m_shutter[1];
        if (derivs)
            memset((char*)val + type.size(), 0, 2 * type.size());
        return true;
    }
    return false;
}


bool
OSLToyRenderer::get_camera_shutter_open(ShaderGlobals* /*sg*/, bool derivs,
                                        ustringhash /*object*/, TypeDesc type,
                                        ustringhash /*name*/, void* val)
{
    if (type == TypeFloat) {
        ((float*)val)[0] = m_shutter[0];
        if (derivs)
            memset((char*)val + type.size(), 0, 2 * type.size());
        return true;
    }
    return false;
}


bool
OSLToyRenderer::get_camera_shutter_close(ShaderGlobals* /*sg*/, bool derivs,
                                         ustringhash /*object*/, TypeDesc type,
                                         ustringhash /*name*/, void* val)
{
    if (type == TypeFloat) {
        ((float*)val)[0] = m_shutter[1];
        if (derivs)
            memset((char*)val + type.size(), 0, 2 * type.size());
        return true;
    }
    return false;
}


bool
OSLToyRenderer::get_camera_screen_window(ShaderGlobals* /*sg*/, bool derivs,
                                         ustringhash /*object*/, TypeDesc type,
                                         ustringhash /*name*/, void* val)
{
    // N.B. in a real renderer, this may be time-dependent
    if (type == TypeFloatArray4) {
        ((float*)val)[0] = m_screen_window[0];
        ((float*)val)[1] = m_screen_window[1];
        ((float*)val)[2] = m_screen_window[2];
        ((float*)val)[3] = m_screen_window[3];
        if (derivs)
            memset((char*)val + type.size(), 0, 2 * type.size());
        return true;
    }
    return false;
}


OSL_NAMESPACE_END

#include "../glx/hardext.h"
#include "array.h"
#include "enum_info.h"
#include "fpe.h"
#include "gl4es.h"
#include "gles.h"
#include "glstate.h"
#include "init.h"
#include "list.h"
#include "loader.h"
#include "render.h"

static GLboolean is_cache_compatible(GLsizei count) {
    #define T2(AA, A, B) \
    if(glstate->vao->AA!=glstate->vao->B.enabled) return GL_FALSE; \
    if(glstate->vao->B.enabled && memcmp(&glstate->vao->pointers[A], &glstate->vao->B.state, sizeof(pointer_state_t))) return GL_FALSE;
    #define TEST(A,B) T2(pointers[A].enabled, A, B)
    #define TESTA(A,B,I) T2(pointers[A+i].enabled, A+i, B[i])

    if(glstate->vao == glstate->defaultvao) return GL_FALSE;
    if(count > glstate->vao->cache_count) return GL_FALSE;
    TEST(ATT_VERTEX, vert)
    TEST(ATT_COLOR, color)
    TEST(ATT_SECONDARY, secondary)
    TEST(ATT_FOGCOORD, fog)
    TEST(ATT_NORMAL, normal)
    for (int i=0; i<hardext.maxtex; i++) {
        TESTA(ATT_MULTITEXCOORD0,tex,i)
    }
    #undef TESTA
    #undef TEST
    #undef T2
    return GL_TRUE;
}

static GLboolean is_list_compatible(renderlist_t* list) {
    #define T2(AA, A, B) \
    if(glstate->vao->AA!=(list->B!=NULL)) return GL_FALSE;
    #define TEST(A,B) T2(pointers[A].enabled, A, B)
    #define TESTA(A,B,I) T2(pointers[A+i].enabled, A+i, B[i])

    if(list->post_color && !list->color) return GL_FALSE;
    if(list->post_normal && !list->normal) return GL_FALSE;
    TEST(ATT_VERTEX, vert)
    TEST(ATT_COLOR, color)
    TEST(ATT_SECONDARY, secondary)
    TEST(ATT_FOGCOORD, fogcoord)
    TEST(ATT_NORMAL, normal)
    for (int i=0; i<hardext.maxtex; i++) {
        TESTA(ATT_MULTITEXCOORD0,tex,i)
    }
    #undef TESTA
    #undef TEST
    #undef T2
    return GL_TRUE;
}

static renderlist_t *arrays_to_renderlist(renderlist_t *list, GLenum mode,
                                        GLsizei skip, GLsizei count) {
    if (! list)
        list = alloc_renderlist();
//LOGD("arrary_to_renderlist, compiling=%d, skip=%d, count=%d\n", glstate->list.compiling, skip, count);
    list->mode = mode;
    list->mode_init = mode;
    list->mode_dimension = rendermode_dimensions(mode);
    list->len = count-skip;
    list->cap = count-skip;

    // check cache if any
    if(glstate->vao->shared_arrays)  {
        if (!is_cache_compatible(count))
            VaoSharedClear(glstate->vao);
    }
    
    if(glstate->vao->shared_arrays) {
        #define OP(A, N) (A)?(A+skip*N):NULL
        list->vert = OP(glstate->vao->vert.ptr,4);
        list->color = OP(glstate->vao->color.ptr,4);
        list->secondary = OP(glstate->vao->secondary.ptr,4);
        list->fogcoord = OP(glstate->vao->fog.ptr, 1);
        list->normal = OP(glstate->vao->normal.ptr,3);
        for (int i=0; i<hardext.maxtex; i++) 
            list->tex[i] = OP(glstate->vao->tex[i].ptr,4);
        #undef OP
        
        list->shared_arrays = glstate->vao->shared_arrays;
        (*glstate->vao->shared_arrays)++;
    } else {
        if(!globals4es.novaocache && glstate->vao != glstate->defaultvao) {
            // prepare a vao cache object
            list->shared_arrays = glstate->vao->shared_arrays = (int*)malloc(sizeof(int));
            *glstate->vao->shared_arrays = 2; // already shared between glstate & list
            #define G2(AA, A, B) \
            glstate->vao->B.enabled = glstate->vao->pointers[AA].enabled; \
            if (glstate->vao->B.enabled) memcpy(&glstate->vao->B.state, &glstate->vao->pointers[A], sizeof(pointer_state_t));
            #define GO(A,B) G2(A, A, B)
            #define GOA(A,B,I) G2(A+i, A+i, B[i])
            GO(ATT_VERTEX, vert)
            GO(ATT_COLOR, color)
            GO(ATT_SECONDARY, secondary)
            GO(ATT_FOGCOORD, fog)
            GO(ATT_NORMAL, normal)
            for (int i=0; i<hardext.maxtex; i++) {
                GOA(ATT_MULTITEXCOORD0,tex,i)
            }
            glstate->vao->cache_count = count;
            #undef GOA
            #undef GO
            #undef G2
        }
        if (glstate->vao->pointers[ATT_VERTEX].enabled) {
            if(glstate->vao->shared_arrays) {
                glstate->vao->vert.ptr = copy_gl_pointer_tex(&glstate->vao->pointers[ATT_VERTEX], 4, 0, count);
                list->vert = glstate->vao->vert.ptr + 4*skip;
            } else
                list->vert = copy_gl_pointer_tex(&glstate->vao->pointers[ATT_VERTEX], 4, skip, count);
        }
        if (glstate->vao->pointers[ATT_COLOR].enabled) {
            if(glstate->vao->shared_arrays) {
                if(glstate->vao->pointers[ATT_COLOR].size==GL_BGRA)
                    glstate->vao->color.ptr = copy_gl_pointer_color_bgra(glstate->vao->pointers[ATT_COLOR].pointer, glstate->vao->pointers[ATT_COLOR].stride, 4, 0, count);
                else
                    glstate->vao->color.ptr = copy_gl_pointer_color(&glstate->vao->pointers[ATT_COLOR], 4, 0, count);
                list->color = glstate->vao->color.ptr + 4*skip;
            } else {
                if(glstate->vao->pointers[ATT_COLOR].size==GL_BGRA)
                    list->color = copy_gl_pointer_color_bgra(glstate->vao->pointers[ATT_COLOR].pointer, glstate->vao->pointers[ATT_COLOR].stride, 4, skip, count);
                else
                    list->color = copy_gl_pointer_color(&glstate->vao->pointers[ATT_COLOR], 4, skip, count);
            }
        }
        if (glstate->vao->pointers[ATT_SECONDARY].enabled/* && glstate->enable.color_array*/) {
            if(glstate->vao->shared_arrays) {
                if(glstate->vao->pointers[ATT_SECONDARY].size==GL_BGRA)
                    glstate->vao->secondary.ptr = copy_gl_pointer_color_bgra(glstate->vao->pointers[ATT_SECONDARY].pointer, glstate->vao->pointers[ATT_SECONDARY].stride, 4, 0, count);
                else
                    glstate->vao->secondary.ptr = copy_gl_pointer(&glstate->vao->pointers[ATT_SECONDARY], 4, 0, count);		// alpha chanel is always 0 for secondary...
                    list->secondary = glstate->vao->secondary.ptr + 4*skip;
            } else {
                if(glstate->vao->pointers[ATT_SECONDARY].size==GL_BGRA)
                    list->secondary = copy_gl_pointer_color_bgra(glstate->vao->pointers[ATT_SECONDARY].pointer, glstate->vao->pointers[ATT_SECONDARY].stride, 4, skip, count);
                else
                    list->secondary = copy_gl_pointer(&glstate->vao->pointers[ATT_SECONDARY], 4, skip, count);		// alpha chanel is always 0 for secondary...
            }
        }
        if (glstate->vao->pointers[ATT_NORMAL].enabled) {
            if(glstate->vao->shared_arrays) {
                glstate->vao->normal.ptr = copy_gl_pointer_raw(&glstate->vao->pointers[ATT_NORMAL], 3, 0, count);
                list->normal = glstate->vao->normal.ptr + 3*skip;
            } else
                list->normal = copy_gl_pointer_raw(&glstate->vao->pointers[ATT_NORMAL], 3, skip, count);
        }
        if (glstate->vao->pointers[ATT_FOGCOORD].enabled) {
            if(glstate->vao->shared_arrays) {
                glstate->vao->fog.ptr = copy_gl_pointer_raw(&glstate->vao->pointers[ATT_FOGCOORD], 1, 0, count);
                list->fogcoord = glstate->vao->fog.ptr + 1*skip;
            } else
                list->fogcoord = copy_gl_pointer_raw(&glstate->vao->pointers[ATT_FOGCOORD], 1, skip, count);
        }
        for (int i=0; i<glstate->vao->maxtex; i++) {
            if (glstate->vao->pointers[ATT_MULTITEXCOORD0+i].enabled) {
                if(glstate->vao->shared_arrays) {
                    glstate->vao->tex[i].ptr = copy_gl_pointer_tex(&glstate->vao->pointers[ATT_MULTITEXCOORD0+i], 4, 0, count);
                    list->tex[i] = glstate->vao->tex[i].ptr + 4*skip;
                } else
                    list->tex[i] = copy_gl_pointer_tex(&glstate->vao->pointers[ATT_MULTITEXCOORD0+i], 4, skip, count);
            }
        }
    }
    for (int i=0; i<hardext.maxtex; i++)
        if(list->tex[i] && list->maxtex < i+1) list->maxtex = i+1;
    return list;
}
static renderlist_t *arrays_add_renderlist(renderlist_t *a, GLenum mode,
                                        GLsizei skip, GLsizei count, GLushort* indices, int ilen_b) {
    // check cache if any
    if(glstate->vao->shared_arrays)  {
        if (!is_cache_compatible(count))
            VaoSharedClear(glstate->vao);
    }
    // append all draw elements of b in a
    // check the final indice size of a and b
    int ilen_a = a->ilen;
    int len_b = count-skip;
    // lets append all the arrays
    unsigned long cap = a->cap;
    if (a->len + len_b >= cap) cap += len_b + DEFAULT_RENDER_LIST_CAPACITY;
    // Unshare if shared (shared array are not used for now)
    unshared_renderlist(a, cap);
    redim_renderlist(a, cap);
    unsharedindices_renderlist(a, ((ilen_a)?ilen_a:a->len) + ((ilen_b)?ilen_b:len_b));
    // append arrays
    if(glstate->vao->shared_arrays) {
        if (a->vert) memcpy(a->vert+a->len*4, glstate->vao->vert.ptr+skip*4, len_b*4*sizeof(GLfloat));
        if (a->normal) memcpy(a->normal+a->len*3, glstate->vao->normal.ptr+skip*3, len_b*3*sizeof(GLfloat));
        if (a->color) memcpy(a->color+a->len*4, glstate->vao->color.ptr+skip*4, len_b*4*sizeof(GLfloat));
        if (a->secondary) memcpy(a->secondary+a->len*4, glstate->vao->secondary.ptr+skip*4, len_b*4*sizeof(GLfloat));
        if (a->fogcoord) memcpy(a->fogcoord+a->len*1, glstate->vao->fog.ptr+skip*1, len_b*1*sizeof(GLfloat));
        for (int i=0; i<a->maxtex; i++)
            if (a->tex[i]) memcpy(a->tex[i]+a->len*4, glstate->vao->tex[i].ptr+skip*4, len_b*4*sizeof(GLfloat));
    } else {
        if (a->vert) copy_gl_pointer_tex_noalloc(a->vert+a->len*4, &glstate->vao->pointers[ATT_VERTEX], 4, skip, count);
        if (a->normal) copy_gl_pointer_raw_noalloc(a->normal+a->len*3, &glstate->vao->pointers[ATT_NORMAL], 3, skip, count);
        if (a->color) {
            if(glstate->vao->pointers[ATT_COLOR].size==GL_BGRA)
                copy_gl_pointer_color_bgra_noalloc(a->color+a->len*4, glstate->vao->pointers[ATT_COLOR].pointer, glstate->vao->pointers[ATT_COLOR].stride, 4, skip, count);
            else
                copy_gl_pointer_color_noalloc(a->color+a->len*4, &glstate->vao->pointers[ATT_COLOR], 4, skip, count);
        }
        if (a->secondary)
            if(glstate->vao->pointers[ATT_SECONDARY].size==GL_BGRA)
                copy_gl_pointer_color_bgra_noalloc(a->secondary+a->len*4, glstate->vao->pointers[ATT_SECONDARY].pointer, glstate->vao->pointers[ATT_SECONDARY].stride, 4, skip, count);
            else
                copy_gl_pointer_noalloc(a->secondary+a->len*4, &glstate->vao->pointers[ATT_SECONDARY], 4, skip, count);		// alpha chanel is always 0 for secondary...
        if (a->fogcoord) copy_gl_pointer_raw_noalloc(a->fogcoord+a->len*1, &glstate->vao->pointers[ATT_FOGCOORD], 1, skip, count);
        for (int i=0; i<a->maxtex; i++)
            if (a->tex[i]) copy_gl_pointer_tex_noalloc(a->tex[i]+a->len*4, &glstate->vao->pointers[ATT_MULTITEXCOORD0+i], 4, skip, count);
    }
    // indices
    int old_ilenb = ilen_b;
    if(!a->mode_inits) list_add_modeinit(a, a->mode_init);
    if (ilen_a || ilen_b || mode_needindices(a->mode) || mode_needindices(mode) 
        || (a->mode!=mode && (a->mode==GL_QUADS || mode==GL_QUADS)) )
    {
        // alloc or realloc a->indices first...
        ilen_b = indices_getindicesize(mode, ((indices)? ilen_b:len_b));
        prepareadd_renderlist(a, ilen_b);
        // then append b
        doadd_renderlist(a, mode, indices, indices?old_ilenb:len_b, ilen_b);
    }
    // lenghts
    a->len += len_b;
    if(a->mode_inits) list_add_modeinit(a, mode);
    //all done
    a->stage = STAGE_DRAW;  // just in case
    return a;
}

static inline bool should_intercept_render(GLenum mode) {
    // check bounded tex that will be used if one need some transformations
    if (hardext.esversion==1)   // but only for ES1.1
    for (int aa=0; aa<hardext.maxtex; aa++) {
        if (glstate->enable.texture[aa]) {
            if ((hardext.esversion==1) && ((glstate->enable.texgen_s[aa] || glstate->enable.texgen_t[aa] || glstate->enable.texgen_r[aa] || glstate->enable.texgen_q[aa])))
                return true;
            if ((!glstate->vao->pointers[ATT_MULTITEXCOORD0+aa].enabled) && !(mode==GL_POINT && glstate->texture.pscoordreplace[aa]))
                return true;
            if ((glstate->vao->pointers[ATT_MULTITEXCOORD0+aa].enabled) && (glstate->vao->pointers[ATT_MULTITEXCOORD0+aa].size == 1))
                return true;
        }
    }
    if(glstate->polygon_mode == GL_LINE && mode>=GL_TRIANGLES)
        return true;
    if ((hardext.esversion==1) && ((glstate->vao->pointers[ATT_SECONDARY].enabled) && (glstate->vao->pointers[ATT_COLOR].enabled)))
        return true;
    if ((hardext.esversion==1) && (glstate->vao->pointers[ATT_COLOR].enabled && (glstate->vao->pointers[ATT_COLOR].size != 4)))
        return true;
    return (
        (glstate->vao->pointers[ATT_VERTEX].enabled && ! valid_vertex_type(glstate->vao->pointers[ATT_VERTEX].type)) ||
        (mode == GL_LINES && glstate->enable.line_stipple) ||
        /*(mode == GL_QUADS) ||*/ (glstate->list.active && !glstate->list.pending)
    );
}

GLuint len_indices(const GLushort *sindices, const GLuint *iindices, GLsizei count) {
    GLuint len = 0;
    if (sindices) {
        for (int i=0; i<count; i++)
            if (len<sindices[i]) len = sindices[i]; // get the len of the arrays
    } else {
        for (int i=0; i<count; i++)
            if (len<iindices[i]) len = iindices[i]; // get the len of the arrays
    }
    return len+1;  // lenght is max(indices) + 1 !
}

static void glDrawElementsCommon(GLenum mode, GLint first, GLsizei count, GLuint len, const GLushort *sindices, const GLuint *iindices, int instancecount) {
    if (glstate->raster.bm_drawing)
        bitmap_flush();
    //printf("glDrawElementsCommon(%s, %d, %d, %d, %p, %p, %d)\n", PrintEnum(mode), first, count, len, sindices, iindices, instancecount);
    LOAD_GLES_FPE(glDrawElements);
    LOAD_GLES_FPE(glDrawArrays);
    LOAD_GLES_FPE(glNormalPointer);
    LOAD_GLES_FPE(glVertexPointer);
    LOAD_GLES_FPE(glColorPointer);
    LOAD_GLES_FPE(glTexCoordPointer);
    LOAD_GLES_FPE(glEnable);
    LOAD_GLES_FPE(glDisable);
    LOAD_GLES_FPE(glEnableClientState);
    LOAD_GLES_FPE(glDisableClientState);
    LOAD_GLES_FPE(glMultiTexCoord4f);
#define client_state(A, B, C) \
        if(glstate->vao->pointers[A].enabled != glstate->clientstate[A]) {   \
            C                                               \
            glstate->clientstate[A] = glstate->vao->pointers[A].enabled;     \
            if(glstate->clientstate[A])                     \
                gles_glEnableClientState(B);                \
            else                                            \
                gles_glDisableClientState(B);               \
        }
#if 0
// FEZ draw the stars (intro menu and the ones visible by night)
// by drawing a huge list of 500k+ triangles!
// it's a bit too much for mobile hardware, so it can be simply disabled here
if(count>500000) return;
#endif
    GLenum mode_init = mode;
    /*if (glstate->polygon_mode == GL_LINE && mode>=GL_TRIANGLES)
        mode = GL_LINE_LOOP;*/
    if (glstate->polygon_mode == GL_POINT && mode>=GL_TRIANGLES)
        mode = GL_POINTS;

    if (mode == GL_QUAD_STRIP)
        mode = GL_TRIANGLE_STRIP;
    if (mode == GL_POLYGON)
        mode = GL_TRIANGLE_FAN;
    if (mode == GL_QUADS) {
        mode = GL_TRIANGLES;
        int ilen = (count*3)/2;
        if (iindices) {
            gl4es_scratch(ilen*sizeof(GLuint));
            GLuint *tmp = (GLuint*)glstate->scratch;
            for (int i=0, j=0; i+3<count; i+=4, j+=6) {
                tmp[j+0] = iindices[i+0];
                tmp[j+1] = iindices[i+1];
                tmp[j+2] = iindices[i+2];

                tmp[j+3] = iindices[i+0];
                tmp[j+4] = iindices[i+2];
                tmp[j+5] = iindices[i+3];
            }
            iindices = tmp;
        } else {
            gl4es_scratch(ilen*sizeof(GLushort));
            GLushort *tmp = (GLushort*)glstate->scratch;
            for (int i=0, j=0; i+3<count; i+=4, j+=6) {
                tmp[j+0] = sindices[i+0];
                tmp[j+1] = sindices[i+1];
                tmp[j+2] = sindices[i+2];

                tmp[j+3] = sindices[i+0];
                tmp[j+4] = sindices[i+2];
                tmp[j+5] = sindices[i+3];
            }
            sindices = tmp;
        }
        count = ilen;
    }
    // of course, GL_SELECT with shader will just not work if not using standard transformation method... Instance count is ignored also
    if (glstate->render_mode == GL_SELECT) {
        // TODO handling uint indices
        if(!sindices && !iindices)
            select_glDrawArrays(&glstate->vao->pointers[ATT_VERTEX], mode, first, count);
        else
            select_glDrawElements(&glstate->vao->pointers[ATT_VERTEX], mode, count, sindices?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT, sindices?((void*)sindices):((void*)iindices));
    } else {
        GLuint old_tex = glstate->texture.client;
        
        realize_textures();

        // check if arrays are locked and can be put in a VBO
        if(hardext.esversion>1 && globals4es.usevbo==2 && glstate->vao->locked==1) {
            // can now browse all enabled VA, and put the corresponding data in a VBO
            // warning, with the use of first 
            // Checking only Vertex Attrib for now!
            // TODO: check all va, and take care of interleaved ones...
            ToBuffer(glstate->vao->first, glstate->vao->count);
        }

        #define TEXTURE(A) gl4es_glClientActiveTexture(A+GL_TEXTURE0);

        pointer_state_t *p;
        #define GetP(A) (&glstate->vao->pointers[A])
        // secondary color and color sizef != 4 are "intercepted" and draw using a list, unless usin ES>1.1
        client_state(ATT_COLOR, GL_COLOR_ARRAY, );
        p = GetP(ATT_COLOR);
        if (p->enabled)
            gles_glColorPointer(p->size, p->type, p->stride, p->pointer);
        if(hardext.esversion>1) {
            client_state(ATT_SECONDARY, GL_SECONDARY_COLOR_ARRAY, );
            p = GetP(ATT_SECONDARY);
            if (p->enabled)
                fpe_glSecondaryColorPointer(p->size, p->type, p->stride, p->pointer);
        }
        client_state(ATT_NORMAL, GL_NORMAL_ARRAY, );
        p = GetP(ATT_NORMAL);
        if (p->enabled)
            gles_glNormalPointer(p->type, p->stride, p->pointer);
        client_state(ATT_VERTEX, GL_VERTEX_ARRAY, );
        p = GetP(ATT_VERTEX);
        if (p->enabled)
            gles_glVertexPointer(p->size, p->type, p->stride, p->pointer);
        for (int aa=0; aa<hardext.maxtex; aa++) {
            client_state(ATT_MULTITEXCOORD0+aa, GL_TEXTURE_COORD_ARRAY, TEXTURE(aa););
            p = GetP(ATT_MULTITEXCOORD0+aa);
            // get 1st enabled target
            const GLint itarget = get_target(glstate->enable.texture[aa]);
            if (itarget>=0) {
                if (!IS_TEX2D(glstate->enable.texture[aa]) && (IS_ANYTEX(glstate->enable.texture[aa]))) {
                    gl4es_glActiveTexture(GL_TEXTURE0+aa);
                    realize_active();
                    gles_glEnable(GL_TEXTURE_2D);
                }
                if (p->enabled) {
                    TEXTURE(aa);
                    int changes = tex_setup_needchange(itarget);
                    if(changes && !len) len = len_indices(sindices, iindices, count);
                    tex_setup_texcoord(len, changes, itarget, p);
                } else
                    gles_glMultiTexCoord4f(GL_TEXTURE0+aa, glstate->texcoord[aa][0], glstate->texcoord[aa][1], glstate->texcoord[aa][2], glstate->texcoord[aa][3]);
            } else if (glstate->clientstate[ATT_MULTITEXCOORD0+aa] && hardext.esversion!=1) {
                // special case on GL2, Tex disable but CoordArray enabled...
                TEXTURE(aa);
                int changes = tex_setup_needchange(ENABLED_TEX2D);
                if(changes && !len) len = len_indices(sindices, iindices, count);
                tex_setup_texcoord(len, changes, ENABLED_TEX2D, p);
            }
        }
        #undef GetP
        if (glstate->texture.client!=old_tex)
            TEXTURE(old_tex);

        // POLYGON mode as LINE is "intercepted" and drawn using list
        if(instancecount==1) {
            if(!iindices && !sindices)
                gles_glDrawArrays(mode, first, count);
            else
                gles_glDrawElements(mode, count, (sindices)?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT, (sindices?((void*)sindices):((void*)iindices)));
        } else {
            if(!iindices && !sindices)
                for (glstate->instanceID=0; glstate->instanceID<instancecount; ++glstate->instanceID)
                    gles_glDrawArrays(mode, first, count);
            else {
                void* tmp=(sindices?((void*)sindices):((void*)iindices));
                GLenum t = (sindices)?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT;
                for (glstate->instanceID=0; glstate->instanceID<instancecount; ++glstate->instanceID)
                    gles_glDrawElements(mode, count, t, tmp);
            }
            glstate->instanceID = 0;
        }

        for (int aa=0; aa<hardext.maxtex; aa++) {
            if (!IS_TEX2D(glstate->enable.texture[aa]) && (IS_ANYTEX(glstate->enable.texture[aa]))) {
                gl4es_glActiveTexture(GL_TEXTURE0+aa);
                realize_active();
                gles_glDisable(GL_TEXTURE_2D);
            }
        }
        if (glstate->texture.client!=old_tex)
            TEXTURE(old_tex);
        #undef TEXTURE
    }
}

#define MIN_BATCH  globals4es.minbatch
#define MAX_BATCH  globals4es.maxbatch

void gl4es_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices) {
    //printf("glDrawRangeElements(%s, %i, %i, %i, %s, @%p), inlist=%i, pending=%d\n", PrintEnum(mode), start, end, count, PrintEnum(type), indices, (glstate->list.active)?1:0, glstate->list.pending);
    count = adjust_vertices(mode, count);
    
    if (count<0) {
		errorShim(GL_INVALID_VALUE);
		return;
	}
    if (count==0) {
        noerrorShim();
        return;
    }

    bool compiling = (glstate->list.active);
    bool intercept = should_intercept_render(mode);

    //BATCH Mode
    if(!compiling) {
        if((!intercept && !glstate->list.pending && (count>=MIN_BATCH && count<=MAX_BATCH)) 
            || (intercept && globals4es.maxbatch)) {
            compiling = true;
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }

	noerrorShim();
    GLushort *sindices = NULL;
    GLuint *iindices = NULL;
    bool need_free = !(
        (type==GL_UNSIGNED_SHORT) || 
        (!compiling && !intercept && type==GL_UNSIGNED_INT && hardext.elementuint)
        );
    if(need_free) {
        sindices = copy_gl_array((glstate->vao->elements)?glstate->vao->elements->data + (uintptr_t)indices:indices,
            type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);
    } else {
        if(type==GL_UNSIGNED_INT)
            iindices = (glstate->vao->elements)?(glstate->vao->elements->data + (uintptr_t)indices):(GLvoid*)indices;
        else
            sindices = (glstate->vao->elements)?(glstate->vao->elements->data + (uintptr_t)indices):(GLvoid*)indices;
    }

    if (compiling) {
        // TODO, handle uint indices
        renderlist_t *list = glstate->list.active;

        if(!need_free) {
            GLushort *tmp = sindices;
            sindices = (GLushort*)malloc(count*sizeof(GLushort));
            memcpy(sindices, tmp, count*sizeof(GLushort));
        }
        for (int i=0; i<count; i++) sindices[i]-=start; //TODO: should be optimizable

        if(globals4es.mergelist && list->stage>=STAGE_DRAW && is_list_compatible(list) && !list->use_glstate && sindices) {
            list = NewDrawStage(list, mode);
            if(list->vert) {
                glstate->list.active = arrays_add_renderlist(list, mode, start, end + 1, sindices, count);
                NewStage(glstate->list.active, STAGE_POSTDRAW);
                return;
            }
        }

		NewStage(list, STAGE_DRAW);

        glstate->list.active = list = arrays_to_renderlist(list, mode, start, end + 1);
        list->indices = sindices;
        list->ilen = count;
        list->indice_cap = count;
        //end_renderlist(list);
        
        NewStage(glstate->list.active, STAGE_POSTDRAW);
        return;
    }

    if (intercept) {
         //TODO handling uint indices
        renderlist_t *list = NULL;

        if(!need_free) {
            GLushort *tmp = sindices;
            sindices = (GLushort*)malloc(count*sizeof(GLushort));
            memcpy(sindices, tmp, count*sizeof(GLushort));
        }
        for (int i=0; i<count; i++) sindices[i]-=start;
        list = arrays_to_renderlist(list, mode, start, end + 1);
        list->indices = sindices;
        list->ilen = count;
        list->indice_cap = count;
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
        
        return;
    } else {
        glDrawElementsCommon(mode, 0, count, end+1, sindices, iindices, 1);
        if(need_free)
            free(sindices);
    }
}
void glDrawRangeElements(GLenum mode,GLuint start,GLuint end,GLsizei count,GLenum type,const void *indices) AliasExport("gl4es_glDrawRangeElements");
void glDrawRangeElementsEXT(GLenum mode,GLuint start,GLuint end,GLsizei count,GLenum type,const void *indices) AliasExport("gl4es_glDrawRangeElements");


void gl4es_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    //printf("glDrawElements(%s, %d, %s, %p), vtx=%p map=%p, pending=%d\n", PrintEnum(mode), count, PrintEnum(type), indices, (glstate->vao->vertex)?glstate->vao->vertex->data:NULL, (glstate->vao->elements)?glstate->vao->elements->data:NULL, glstate->list.pending);
    // TODO: split for count > 65535?
    // special check for QUADS and TRIANGLES that need multiple of 4 or 3 vertex...
    count = adjust_vertices(mode, count);
    
    if (count<0) {
		errorShim(GL_INVALID_VALUE);
		return;
	}
    if (count==0) {
        noerrorShim();
        return;
    }

    bool compiling = (glstate->list.active);
    bool intercept = should_intercept_render(mode);

    //BATCH Mode
    if(!compiling) {
        if((!intercept && !glstate->list.pending && (count>=MIN_BATCH && count<=MAX_BATCH)) 
            || (intercept && globals4es.maxbatch)) {
            compiling = true;
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }

	noerrorShim();
    GLushort *sindices = NULL;
    GLuint *iindices = NULL;
    bool need_free = !(
        (type==GL_UNSIGNED_SHORT) || 
        (!compiling && !intercept && type==GL_UNSIGNED_INT && hardext.elementuint)
        );
    if(need_free) {
        sindices = copy_gl_array((glstate->vao->elements)?glstate->vao->elements->data + (uintptr_t)indices:indices,
            type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);
    } else {
        if(type==GL_UNSIGNED_INT)
            iindices = (glstate->vao->elements)?(glstate->vao->elements->data + (uintptr_t)indices):(GLvoid*)indices;
        else
            sindices = (glstate->vao->elements)?(glstate->vao->elements->data + (uintptr_t)indices):(GLvoid*)indices;
    }

    if (compiling) {
        // TODO, handle uint indices
        renderlist_t *list = glstate->list.active;
        GLsizei min, max;

        if(!need_free) {
            GLushort *tmp = sindices;
            sindices = (GLushort*)malloc(count*sizeof(GLushort));
            memcpy(sindices, tmp, count*sizeof(GLushort));
        }

        normalize_indices_us(sindices, &max, &min, count);

        if(globals4es.mergelist && list->stage>=STAGE_DRAW && is_list_compatible(list) && !list->use_glstate && sindices) {
            list = NewDrawStage(list, mode);
            glstate->list.active = arrays_add_renderlist(list, mode, min, max + 1, sindices, count);
            NewStage(glstate->list.active, STAGE_POSTDRAW);
            return;
        }

		NewStage(list, STAGE_DRAW);

        glstate->list.active = list = arrays_to_renderlist(list, mode, min, max + 1);
        list->indices = sindices;
        list->ilen = count;
        list->indice_cap = count;
        //end_renderlist(list);
        
        NewStage(glstate->list.active, STAGE_POSTDRAW);
        return;
    }

    if (intercept) {
         //TODO handling uint indices
        renderlist_t *list = NULL;
        GLsizei min, max;

        if(!need_free) {
            GLushort *tmp = sindices;
            sindices = (GLushort*)malloc(count*sizeof(GLushort));
            memcpy(sindices, tmp, count*sizeof(GLushort));
        }
        normalize_indices_us(sindices, &max, &min, count);
        list = arrays_to_renderlist(list, mode, min, max + 1);
        list->indices = sindices;
        list->ilen = count;
        list->indice_cap = count;
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
        return;
    } else {
        glDrawElementsCommon(mode, 0, count, 0, sindices, iindices, 1);
        if(need_free)
            free(sindices);
    }
}
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) AliasExport("gl4es_glDrawElements");

void gl4es_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    //printf("glDrawArrays(%s, %d, %d), list=%p pending=%d\n", PrintEnum(mode), first, count, glstate->list.active, glstate->list.pending);
    // special check for QUADS and TRIANGLES that need multiple of 4 or 3 vertex...
    count = adjust_vertices(mode, count);

	if (count<0) {
		errorShim(GL_INVALID_VALUE);
		return;
	}
    if (count==0) {
        noerrorShim();
        return;
    }

    // special case for (very) large GL_QUADS array
    if ((mode==GL_QUADS) && (count>4*8000)) {
        // split the array in manageable slice
        int cnt = 4*8000;
        for (int i=0; i<count; i+=4*8000) {
            if (i+cnt>count) cnt = count-i;
            gl4es_glDrawArrays(mode, i, cnt);
        }
        return;
    }
	noerrorShim();

    bool intercept = should_intercept_render(mode);
    //BATCH Mode
    if (!glstate->list.compiling) {
        if((!intercept && !glstate->list.pending && (count>=MIN_BATCH && count<=MAX_BATCH)) 
            || (intercept && globals4es.maxbatch)) {
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }

    if (glstate->list.active) {
        renderlist_t *list = glstate->list.active;
        
        if(globals4es.mergelist && list->stage>=STAGE_DRAW && is_list_compatible(list) && !list->use_glstate) {
            list = NewDrawStage(list, mode);
            if(list->vert) {
                glstate->list.active = arrays_add_renderlist(list, mode, first, count+first, NULL, 0);
                NewStage(glstate->list.active, STAGE_POSTDRAW);
                return;
            }
        }

        NewStage(list, STAGE_DRAW);
        glstate->list.active = arrays_to_renderlist(list, mode, first, count+first);
        NewStage(glstate->list.active, STAGE_POSTDRAW);
        return;
    }

    /*if (glstate->polygon_mode == GL_LINE && mode>=GL_TRIANGLES)
		mode = GL_LINE_LOOP;*/
    if (glstate->polygon_mode == GL_POINT && mode>=GL_TRIANGLES)
		mode = GL_POINTS;

    if (intercept) {
        renderlist_t *list;
        list = arrays_to_renderlist(NULL, mode, first, count+first);
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
    } else {
        if (mode==GL_QUADS) {
            // TODO: move those static in glstate
            static GLushort *indices = NULL;
            static int indcnt = 0;
            static int indfirst = 0;
            int realfirst = ((first%4)==0)?0:first;
            int realcount = count + (first-realfirst);
            if((indcnt < realcount) || (indfirst!=realfirst)) {
                if(indcnt < realcount) {
                    indcnt = realcount;
                    if (indices) free(indices);
                    indices = (GLushort*)malloc(sizeof(GLushort)*(indcnt*3/2));
                }
                indfirst = realfirst;
                GLushort *p = indices;
                for (int i=0, j=indfirst; i+3<indcnt; i+=4, j+=4) {
                        *(p++) = j + 0;
                        *(p++) = j + 1;
                        *(p++) = j + 2;

                        *(p++) = j + 0;
                        *(p++) = j + 2;
                        *(p++) = j + 3;
                }
            }
            glDrawElementsCommon(GL_TRIANGLES, 0, count*3/2, count, indices+(first-indfirst)*3/2, NULL, 1);
            return;
        }

        glDrawElementsCommon(mode, first, count, count, NULL, NULL, 1);
    }
}
void glDrawArrays(GLenum mode, GLint first, GLsizei count) AliasExport("gl4es_glDrawArrays");
void glDrawArraysEXT(GLenum mode, GLint first, GLsizei count) AliasExport("gl4es_glDrawArrays");

void gl4es_glMultiDrawArrays(GLenum mode, const GLint *firsts, const GLsizei *counts, GLsizei primcount)
{
    if(!primcount) {
        noerrorShim();
        return;
    }
    bool compiling = (glstate->list.active);
    bool intercept = should_intercept_render(mode);

    GLsizei maxcount=counts[0];
    GLsizei mincount=counts[0];
    for (int i=1; i<primcount; i++) {
        if(counts[i]>maxcount) maxcount=counts[i];
        if(counts[i]<mincount) mincount=counts[i];
    }
    //BATCH Mode
    if(!compiling) {
        if(!intercept && glstate->list.pending && maxcount>MAX_BATCH)    // too large and will not intercept, stop the BATCH
            flush();
        else if((!intercept && !glstate->list.pending && mincount<MIN_BATCH) 
            || (intercept && globals4es.maxbatch)) {
            compiling = true;
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }
    renderlist_t *list = NULL;

    GLenum err = 0;
       
    for (int i=0; i<primcount; i++) {
        GLsizei count = adjust_vertices(mode, counts[i]);
        GLint first = firsts[i];

        if (count<0) {
            err = GL_INVALID_VALUE;
            continue;
        }
        if (count==0) {
            continue;
        }

        if (compiling) {
            if(globals4es.mergelist && glstate->list.active->stage>=STAGE_DRAW && is_list_compatible(glstate->list.active) && !glstate->list.active->use_glstate) {
                glstate->list.active = NewDrawStage(glstate->list.active, mode);
                glstate->list.active = arrays_add_renderlist(glstate->list.active, mode, first, count+first, NULL, 0);
                NewStage(glstate->list.active, STAGE_POSTDRAW);
                continue;
            }

            NewStage(glstate->list.active, STAGE_DRAW);
            glstate->list.active = arrays_to_renderlist(glstate->list.active, mode, first, count+first);
            NewStage(glstate->list.active, STAGE_POSTDRAW);

            continue;
        }

        if (glstate->polygon_mode == GL_POINT && mode>=GL_TRIANGLES)
            mode = GL_POINTS;

        if (intercept) {
            if(list) {
                NewStage(list, STAGE_DRAW);
            }
            if(globals4es.mergelist && list->stage>=STAGE_DRAW && is_list_compatible(list) && !list->use_glstate) {
                list = NewDrawStage(list, mode);
                list = arrays_add_renderlist(list, mode, first, count+first, NULL, 0);
                NewStage(list, STAGE_POSTDRAW);
            }
            else
                list = arrays_to_renderlist(NULL, mode, first, count+first);
        } else {
            if (mode==GL_QUADS) {
                // TODO: move those static in glstate
                static GLushort *indices = NULL;
                static int indcnt = 0;
                static int indfirst = 0;
                int realfirst = ((first%4)==0)?0:first;
                int realcount = count + (first-realfirst);
                if((indcnt < realcount) || (indfirst!=realfirst)) {
                    if(indcnt < realcount) {
                        indcnt = realcount;
                        if (indices) free(indices);
                        indices = (GLushort*)malloc(sizeof(GLushort)*(indcnt*3/2));
                    }
                    indfirst = realfirst;
                    GLushort *p = indices;
                    for (int i=0, j=indfirst; i+3<indcnt; i+=4, j+=4) {
                            *(p++) = j + 0;
                            *(p++) = j + 1;
                            *(p++) = j + 2;

                            *(p++) = j + 0;
                            *(p++) = j + 2;
                            *(p++) = j + 3;
                    }
                }
                glDrawElementsCommon(GL_TRIANGLES, 0, count*3/2, count, indices+(first-indfirst)*3/2, NULL, 1);
                continue;
            }

            glDrawElementsCommon(mode, first, count, count, NULL, NULL, 1);
        }
    }
    if(list) {
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
    }
    if(err)
        errorShim(err);
    else
        errorGL();
}
void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount) AliasExport("gl4es_glMultiDrawArrays");

void gl4es_glMultiDrawElements( GLenum mode, GLsizei *counts, GLenum type, const void * const *indices, GLsizei primcount)
{
    if(!primcount) {
        noerrorShim();
        return;
    }
    bool compiling = (glstate->list.active);
    bool intercept = should_intercept_render(mode);

    GLsizei maxcount=counts[0];
    GLsizei mincount=counts[0];
    for (int i=1; i<primcount; i++) {
        if(counts[i]>maxcount) maxcount=counts[i];
        if(counts[i]<mincount) mincount=counts[i];
    }
    //BATCH Mode
    if(!compiling) {
        if(!intercept && glstate->list.pending && maxcount>MAX_BATCH)    // too large and will not intercept, stop the BATCH
            flush();
        else if((!intercept && !glstate->list.pending && mincount<MIN_BATCH) 
            || (intercept && globals4es.maxbatch)) {
            compiling = true;
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }
    renderlist_t *list = NULL;
    for (int i=0; i<primcount; i++) {
        GLsizei count = adjust_vertices(mode, counts[i]);
        
        if (count<0) {
            errorShim(GL_INVALID_VALUE);
            continue;
        }
        if (count==0) {
            noerrorShim();
            continue;
        }

        noerrorShim();
        GLushort *sindices = NULL;
        GLuint *iindices = NULL;
        bool need_free = !(
            (type==GL_UNSIGNED_SHORT) || 
            (!compiling && !intercept && type==GL_UNSIGNED_INT && hardext.elementuint)
            );
        if(need_free) {
            sindices = copy_gl_array((glstate->vao->elements)?glstate->vao->elements->data + (uintptr_t)indices:indices,
                type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);
        } else {
            if(type==GL_UNSIGNED_INT)
                iindices = (glstate->vao->elements)?(glstate->vao->elements->data + (uintptr_t)indices):(GLvoid*)indices;
            else
                sindices = (glstate->vao->elements)?(glstate->vao->elements->data + (uintptr_t)indices):(GLvoid*)indices;
        }

        if (compiling) {
            // TODO, handle uint indices
            renderlist_t *list = NULL;
            GLsizei min, max;

            NewStage(glstate->list.active, STAGE_DRAW);
            list = glstate->list.active;

            if(!need_free) {
                GLushort *tmp = sindices;
                sindices = (GLushort*)malloc(count*sizeof(GLushort));
                memcpy(sindices, tmp, count*sizeof(GLushort));
            }
            normalize_indices_us(sindices, &max, &min, count);
            list = arrays_to_renderlist(list, mode, min, max + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            //end_renderlist(list);
            
            if(glstate->list.pending) {
                NewStage(glstate->list.active, STAGE_POSTDRAW);
            } else {
                glstate->list.active = extend_renderlist(list);
            }
            continue;
        }

        if (intercept) {
            //TODO handling uint indices
            renderlist_t *list = NULL;
            GLsizei min, max;

            if(!need_free) {
                GLushort *tmp = sindices;
                sindices = (GLushort*)malloc(count*sizeof(GLushort));
                memcpy(sindices, tmp, count*sizeof(GLushort));
            }
            normalize_indices_us(sindices, &max, &min, count);
            if(list) {
                NewStage(list, STAGE_DRAW);
            }
            list = arrays_to_renderlist(list, mode, min, max + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            continue;
        } else {
            glDrawElementsCommon(mode, 0, count, 0, sindices, iindices, 1);
            if(need_free)
                free(sindices);
        }
    }
    if(list) {
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
    }
}
void glMultiDrawElements( GLenum mode, GLsizei *count, GLenum type, const void * const *indices, GLsizei primcount) AliasExport("gl4es_glMultiDrawElements");

void gl4es_glMultiDrawElementsBaseVertex( GLenum mode, GLsizei *counts, GLenum type, const void * const *indices, GLsizei primcount, const GLint * basevertex) {
    //printf("glMultiDrawElementsBaseVertex(%s, %p, %s, @%p, %d, @%p), inlist=%i, pending=%d\n", PrintEnum(mode), count, PrintEnum(type), indices, primcount, basevertex, (glstate->list.active)?1:0, glstate->list.pending);
    // divide the call, should try something better one day...
    bool compiling = (glstate->list.active);
    bool intercept = should_intercept_render(mode);
    //BATCH Mode
    GLsizei maxcount=counts[0];
    GLsizei mincount=counts[0];
    for (int i=1; i<primcount; i++) {
        if(counts[i]>maxcount) maxcount=counts[i];
        if(counts[i]<mincount) mincount=counts[i];
    }
    if(!compiling) {
        if(!intercept && glstate->list.pending && maxcount>MAX_BATCH)    // too large and will not intercept, stop the BATCH
            flush();
        else if((!intercept && !glstate->list.pending && mincount<MIN_BATCH) 
            || (intercept && globals4es.maxbatch)) {
            compiling = true;
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }
    renderlist_t *list = NULL;
    for (int i=0; i<primcount; i++) {
        GLsizei count = adjust_vertices(mode, counts[i]);
    
        if (count<0) {
            errorShim(GL_INVALID_VALUE);
            continue;
        }
        if (count==0) {
            noerrorShim();
            continue;
        }

        noerrorShim();
        GLushort *sindices = NULL;
        GLuint *iindices = NULL;

        if(type==GL_UNSIGNED_INT && hardext.elementuint && !compiling && !intercept)
            iindices = copy_gl_array((glstate->vao->elements)?glstate->vao->elements->data + (uintptr_t)indices:indices,
                type, 1, 0, GL_UNSIGNED_INT, 1, 0, count, NULL);
        else
            sindices = copy_gl_array((glstate->vao->elements)?glstate->vao->elements->data + (uintptr_t)indices:indices,
                type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);

        if (compiling) {
            // TODO, handle uint indices
            renderlist_t *list = NULL;
            GLsizei min, max;

            NewStage(glstate->list.active, STAGE_DRAW);
            list = glstate->list.active;

            normalize_indices_us(sindices, &max, &min, count);
            list = arrays_to_renderlist(list, mode, min + basevertex[i], max + basevertex[i] + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            //end_renderlist(list);
            
            if(glstate->list.pending) {
                NewStage(glstate->list.active, STAGE_POSTDRAW);
            } else {
                glstate->list.active = extend_renderlist(list);
            }
            continue;
        }

        if (intercept) {
            //TODO handling uint indices
            GLsizei min, max;

            normalize_indices_us(sindices, &max, &min, count);
            if(list) {
                NewStage(list, STAGE_DRAW);
            }
            list = arrays_to_renderlist(list, mode, min + basevertex[i], max + basevertex[i] + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            continue;
        } else {
            if(iindices)
                for(int i=0; i<count; i++) iindices[i]+=basevertex[i];
            else
                for(int i=0; i<count; i++) sindices[i]+=basevertex[i];
            glDrawElementsCommon(mode, 0, count, 0, sindices, iindices, 1);
            if(iindices)
                free(iindices);
            else
                free(sindices);
        }
    }
    if(list) {
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
    }
}
void glMultiDrawElementsBaseVertex( GLenum mode, GLsizei *count, GLenum type, const void * const *indices, GLsizei primcount, const GLint * basevertex) AliasExport("gl4es_glMultiDrawElementsBaseVertex");
void glMultiDrawElementsBaseVertexARB( GLenum mode, GLsizei *count, GLenum type, const void * const *indices, GLsizei primcount, const GLint * basevertex) AliasExport("gl4es_glMultiDrawElementsBaseVertex");

void gl4es_glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex) {
    //printf("glDrawElementsBaseVertex(%s, %d, %s, %p, %d), vtx=%p map=%p, pending=%d\n", PrintEnum(mode), count, PrintEnum(type), indices, basevertex, (glstate->vao->vertex)?glstate->vao->vertex->data:NULL, (glstate->vao->elements)?glstate->vao->elements->data:NULL, glstate->list.pending);
    if(basevertex==0)
        gl4es_glDrawElements(mode, count, type, indices);
    else {
        count = adjust_vertices(mode, count);
       
        if (count<0) {
            errorShim(GL_INVALID_VALUE);
            return;
        }
        if (count==0) {
            noerrorShim();
            return;
        }

        bool compiling = (glstate->list.active);
        bool intercept = should_intercept_render(mode);

        //BATCH Mode
        if(!compiling) {
            if(!intercept && glstate->list.pending && count>MAX_BATCH)    // too large and will not intercept, stop the BATCH
                flush();
            else if((!intercept && !glstate->list.pending && count<MIN_BATCH) 
                || (intercept && globals4es.maxbatch)) {
                compiling = true;
                glstate->list.pending = 1;
                glstate->list.active = alloc_renderlist();
            }
        }

        noerrorShim();
        GLushort *sindices = NULL;
        GLuint *iindices = NULL;

        if(type==GL_UNSIGNED_INT && hardext.elementuint && !compiling && !intercept)
            iindices = copy_gl_array((glstate->vao->elements)?glstate->vao->elements->data + (uintptr_t)indices:indices,
                type, 1, 0, GL_UNSIGNED_INT, 1, 0, count, NULL);
        else
            sindices = copy_gl_array((glstate->vao->elements)?glstate->vao->elements->data + (uintptr_t)indices:indices,
                type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);

        if (compiling) {
            // TODO, handle uint indices
            renderlist_t *list = NULL;
            GLsizei min, max;

            NewStage(glstate->list.active, STAGE_DRAW);
            list = glstate->list.active;

            normalize_indices_us(sindices, &max, &min, count);
            list = arrays_to_renderlist(list, mode, min + basevertex, max + basevertex + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            //end_renderlist(list);
            
            if(glstate->list.pending) {
                NewStage(glstate->list.active, STAGE_POSTDRAW);
            } else {
                glstate->list.active = extend_renderlist(list);
            }
            return;
        }

        if (intercept) {
            //TODO handling uint indices
            renderlist_t *list = NULL;
            GLsizei min, max;

            normalize_indices_us(sindices, &max, &min, count);
            list = arrays_to_renderlist(list, mode, min + basevertex, max + basevertex + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            list = end_renderlist(list);
            draw_renderlist(list);
            free_renderlist(list);
            return;
        } else {
            if(iindices)
                for(int i=0; i<count; i++) iindices[i]+=basevertex;
            else
                for(int i=0; i<count; i++) sindices[i]+=basevertex;
            glDrawElementsCommon(mode, 0, count, 0, sindices, iindices, 1);
            if(iindices)
                free(iindices);
            else
                free(sindices);
        }
    }
}
void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex) AliasExport("gl4es_glDrawElementsBaseVertex");
void glDrawElementsBaseVertexARB(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex) AliasExport("gl4es_glDrawElementsBaseVertex");


void gl4es_glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices, GLint basevertex) {
    //printf("glDrawRangeElementsBaseVertex(%s, %i, %i, %i, %s, @%p, %d), inlist=%i, pending=%d\n", PrintEnum(mode), start, end, count, PrintEnum(type), indices, basevertex, (glstate->list.active)?1:0, glstate->list.pending);
    if(basevertex==0) {
        gl4es_glDrawRangeElements(mode, start, end, count, type, indices);
    } else {
        count = adjust_vertices(mode, count);
        
        if (count<0) {
            errorShim(GL_INVALID_VALUE);
            return;
        }
        if (count==0) {
            noerrorShim();
            return;
        }

        bool compiling = (glstate->list.active);
        bool intercept = should_intercept_render(mode);

        //BATCH Mode
        if(!compiling) {
            if(!intercept && glstate->list.pending && count>MAX_BATCH)    // too large and will not intercept, stop the BATCH
                flush();
            else if((!intercept && !glstate->list.pending && count<MIN_BATCH) 
                || (intercept && globals4es.maxbatch)) {
                compiling = true;
                glstate->list.pending = 1;
                glstate->list.active = alloc_renderlist();
            }
        }

        noerrorShim();
        GLushort *sindices = NULL;
        GLuint *iindices = NULL;
        if(type==GL_UNSIGNED_INT && hardext.elementuint && !compiling && !intercept)
            iindices = copy_gl_array((glstate->vao->elements)?glstate->vao->elements->data + (uintptr_t)indices:indices,
                type, 1, 0, GL_UNSIGNED_INT, 1, 0, count, NULL);
        else
            sindices = copy_gl_array((glstate->vao->elements)?glstate->vao->elements->data + (uintptr_t)indices:indices,
                type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);

        if (compiling) {
            // TODO, handle uint indices
            renderlist_t *list = NULL;

            NewStage(glstate->list.active, STAGE_DRAW);
            list = glstate->list.active;

            for (int i=0; i<count; i++) sindices[i]-=start;
            list = arrays_to_renderlist(list, mode, start + basevertex, end + basevertex + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            //end_renderlist(list);
            
            if(glstate->list.pending) {
                NewStage(glstate->list.active, STAGE_POSTDRAW);
            } else {
                glstate->list.active = extend_renderlist(list);
            }
            return;
        }

        if (intercept) {
            //TODO handling uint indices
            renderlist_t *list = NULL;

            for (int i=0; i<count; i++) sindices[i]-=start;
            list = arrays_to_renderlist(list, mode, start + basevertex, end + basevertex + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            list = end_renderlist(list);
            draw_renderlist(list);
            free_renderlist(list);
            
            return;
        } else {
            if(iindices)
                for(int i=0; i<count; i++) iindices[i]+=basevertex;
            else
                for(int i=0; i<count; i++) sindices[i]+=basevertex;
            glDrawElementsCommon(mode, 0, count, end+basevertex+1, sindices, iindices, 1);
            if(iindices)
                free(iindices);
            else
                free(sindices);
        }
    }
}
void glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices, GLint basevertex) AliasExport("gl4es_glDrawRangeElementsBaseVertex");
void glDrawRangeElementsBaseVertexARB(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices, GLint basevertex) AliasExport("gl4es_glDrawRangeElementsBaseVertex");

void gl4es_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei primcount) {
    count = adjust_vertices(mode, count);

	if (count<0) {
		errorShim(GL_INVALID_VALUE);
		return;
	}
    if (count==0) {
        noerrorShim();
        return;
    }

    // special case for (very) large GL_QUADS array
    if ((mode==GL_QUADS) && (count>4*8000)) {
        // split the array in manageable slice
        int cnt = 4*8000;
        for (int i=0; i<count; i+=4*8000) {
            if (i+cnt>count) cnt = count-i;
            gl4es_glDrawArrays(mode, i, cnt);
        }
        return;
    }
	noerrorShim();

    bool intercept = should_intercept_render(mode);
    //BATCH Mode
    if (!glstate->list.compiling) {
        if((!intercept && !glstate->list.pending && (count>=MIN_BATCH && count<=MAX_BATCH)) 
            || (intercept && globals4es.maxbatch)) {
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }

    if (glstate->list.active) {
        NewStage(glstate->list.active, STAGE_DRAW);
        glstate->list.active = arrays_to_renderlist(glstate->list.active, mode, first, count+first);
        glstate->list.active->instanceCount = primcount;
        if(glstate->list.pending) {
            NewStage(glstate->list.active, STAGE_POSTDRAW);
        } else {
            glstate->list.active = extend_renderlist(glstate->list.active);
        }
        return;
    }

    /*if (glstate->polygon_mode == GL_LINE && mode>=GL_TRIANGLES)
		mode = GL_LINE_LOOP;*/
    if (glstate->polygon_mode == GL_POINT && mode>=GL_TRIANGLES)
		mode = GL_POINTS;

    if (intercept) {
        renderlist_t *list = NULL;
        list = arrays_to_renderlist(list, mode, first, count+first);
        list->instanceCount = primcount;
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
    } else {
        if (mode==GL_QUADS) {
            // TODO: move those static in glstate
            static GLushort *indices = NULL;
            static int indcnt = 0;
            static int indfirst = 0;
            int realfirst = ((first%4)==0)?0:first;
            int realcount = count + (first-realfirst);
            if((indcnt < realcount) || (indfirst!=realfirst)) {
                if(indcnt < realcount) {
                    indcnt = realcount;
                    if (indices) free(indices);
                    indices = (GLushort*)malloc(sizeof(GLushort)*(indcnt*3/2));
                }
                indfirst = realfirst;
                GLushort *p = indices;
                for (int i=0, j=indfirst; i+3<indcnt; i+=4, j+=4) {
                        *(p++) = j + 0;
                        *(p++) = j + 1;
                        *(p++) = j + 2;

                        *(p++) = j + 0;
                        *(p++) = j + 2;
                        *(p++) = j + 3;
                }
            }
            glDrawElementsCommon(GL_TRIANGLES, 0, count*3/2, count, indices+(first-indfirst)*3/2, NULL, primcount);
            return;
        }

        glDrawElementsCommon(mode, first, count, count, NULL, NULL, primcount);
    }
}
void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei primcount) AliasExport("gl4es_glDrawArraysInstanced");
void glDrawArraysInstancedARB(GLenum mode, GLint first, GLsizei count, GLsizei primcount) AliasExport("gl4es_glDrawArraysInstanced");

void gl4es_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount) {
    count = adjust_vertices(mode, count);
    
    if (count<0) {
		errorShim(GL_INVALID_VALUE);
		return;
	}
    if (count==0) {
        noerrorShim();
        return;
    }

    bool compiling = (glstate->list.active);
    bool intercept = should_intercept_render(mode);

    //BATCH Mode
    if(!compiling) {
        if((!intercept && !glstate->list.pending && (count>=MIN_BATCH && count<=MAX_BATCH)) 
            || (intercept && globals4es.maxbatch)) {
            compiling = true;
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }

	noerrorShim();
    GLushort *sindices = NULL;
    GLuint *iindices = NULL;
    bool need_free = !(
        (type==GL_UNSIGNED_SHORT) || 
        (!compiling && !intercept && type==GL_UNSIGNED_INT && hardext.elementuint)
        );
    if(need_free) {
        sindices = copy_gl_array((glstate->vao->elements)?(glstate->vao->elements->data + (uintptr_t)indices):indices,
            type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);
    } else {
        if(type==GL_UNSIGNED_INT)
            iindices = (glstate->vao->elements)?(glstate->vao->elements->data + (uintptr_t)indices):(GLvoid*)indices;
        else
            sindices = (glstate->vao->elements)?(glstate->vao->elements->data + (uintptr_t)indices):(GLvoid*)indices;
    }

    if (compiling) {
        // TODO, handle uint indices
        renderlist_t *list = NULL;
        GLsizei min, max;

		NewStage(glstate->list.active, STAGE_DRAW);
        list = glstate->list.active;

        if(!need_free) {
            GLushort *tmp = sindices;
            sindices = (GLushort*)malloc(count*sizeof(GLushort));
            memcpy(sindices, tmp, count*sizeof(GLushort));
        }
        normalize_indices_us(sindices, &max, &min, count);
        list = arrays_to_renderlist(list, mode, min, max + 1);
        list->indices = sindices;
        list->ilen = count;
        list->indice_cap = count;
        list->instanceCount = primcount;
        //end_renderlist(list);
        
        if(glstate->list.pending) {
            NewStage(glstate->list.active, STAGE_POSTDRAW);
        } else {
            glstate->list.active = extend_renderlist(list);
        }
        return;
    }

    if (intercept) {
         //TODO handling uint indices
        renderlist_t *list = NULL;
        GLsizei min, max;

        if(!need_free) {
            GLushort *tmp = sindices;
            sindices = (GLushort*)malloc(count*sizeof(GLushort));
            memcpy(sindices, tmp, count*sizeof(GLushort));
        }
        normalize_indices_us(sindices, &max, &min, count);
        list = arrays_to_renderlist(list, mode, min, max + 1);
        list->indices = sindices;
        list->ilen = count;
        list->indice_cap = count;
        list->instanceCount = primcount;
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
        return;
    } else {
        glDrawElementsCommon(mode, 0, count, 0, sindices, iindices, primcount);
        if(need_free)
            free(sindices);
    }
}
void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount) AliasExport("gl4es_glDrawElementsInstanced");
void glDrawElementsInstancedARB(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount) AliasExport("gl4es_glDrawElementsInstanced");

void gl4es_glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount, GLint basevertex) {
    //printf("glDrawElementsInstanceBaseVertex(%s, %d, %s, %p, %d, %d), vtx=%p map=%p, pending=%d\n", PrintEnum(mode), count, PrintEnum(type), indices, primcount, basevertex, (glstate->vao->vertex)?glstate->vao->vertex->data:NULL, (glstate->vao->elements)?glstate->vao->elements->data:NULL, glstate->list.pending);
    if(basevertex==0)
        gl4es_glDrawElementsInstanced(mode, count, type, indices, primcount);
    else {
        count = adjust_vertices(mode, count);
       
        if (count<0) {
            errorShim(GL_INVALID_VALUE);
            return;
        }
        if (count==0) {
            noerrorShim();
            return;
        }

        bool compiling = (glstate->list.active);
        bool intercept = should_intercept_render(mode);

        //BATCH Mode
        if(!compiling) {
            if(!intercept && glstate->list.pending && count>MAX_BATCH)    // too large and will not intercept, stop the BATCH
                flush();
            else if((!intercept && !glstate->list.pending && (count>=MIN_BATCH && count<=MAX_BATCH)) 
                || (intercept && globals4es.maxbatch)) {
                compiling = true;
                glstate->list.pending = 1;
                glstate->list.active = alloc_renderlist();
            }
        }

        noerrorShim();
        GLushort *sindices = NULL;
        GLuint *iindices = NULL;

        if(type==GL_UNSIGNED_INT && hardext.elementuint && !compiling && !intercept)
            iindices = copy_gl_array((glstate->vao->elements)?glstate->vao->elements->data + (uintptr_t)indices:indices,
                type, 1, 0, GL_UNSIGNED_INT, 1, 0, count, NULL);
        else
            sindices = copy_gl_array((glstate->vao->elements)?glstate->vao->elements->data + (uintptr_t)indices:indices,
                type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);

        if (compiling) {
            // TODO, handle uint indices
            renderlist_t *list = NULL;
            GLsizei min, max;

            NewStage(glstate->list.active, STAGE_DRAW);
            list = glstate->list.active;

            normalize_indices_us(sindices, &max, &min, count);
            list = arrays_to_renderlist(list, mode, min + basevertex, max + basevertex + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            list->instanceCount = primcount;
            //end_renderlist(list);
            
            if(glstate->list.pending) {
                NewStage(glstate->list.active, STAGE_POSTDRAW);
            } else {
                glstate->list.active = extend_renderlist(list);
            }
            return;
        }

        if (intercept) {
            //TODO handling uint indices
            renderlist_t *list = NULL;
            GLsizei min, max;

            normalize_indices_us(sindices, &max, &min, count);
            list = arrays_to_renderlist(list, mode, min + basevertex, max + basevertex + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            list->instanceCount = primcount;
            list = end_renderlist(list);
            draw_renderlist(list);
            free_renderlist(list);
            return;
        } else {
            if(iindices)
                for(int i=0; i<count; i++) iindices[i]+=basevertex;
            else
                for(int i=0; i<count; i++) sindices[i]+=basevertex;
            glDrawElementsCommon(mode, 0, count, 0, sindices, iindices, primcount);
            if(iindices)
                free(iindices);
            else
                free(sindices);
        }
    }
}
void glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount, GLint basevertex) AliasExport("gl4es_glDrawElementsInstancedBaseVertex");
void glDrawElementsInstancedBaseVertexARB(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount, GLint basevertex) AliasExport("gl4es_glDrawElementsInstancedBaseVertex");

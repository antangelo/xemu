/*
 * xemu User Interface
 *
 * Copyright (C) 2020-2021 Matt Borgerson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xemu-snapshots.h"
#include "xemu-settings.h"
#include "xemu-xbe.h"
#include "hw/xbox/nv2a/gl/gloffscreen.h"

#include <SDL2/SDL.h>
#include <epoxy/gl.h>

#include "qemu-common.h"
#include "qapi/error.h"
#include "block/qapi.h"
#include "block/qdict.h"
#include "block/aio.h"
#include "block/block_int.h"
#include "sysemu/runstate.h"
#include "migration/snapshot.h"
#include "migration/qemu-file.h"

#include "ui/console.h"
#include "ui/input.h"
#include "ui/xemu-display.h"

static QEMUSnapshotInfo *xemu_snapshots_metadata = NULL;
static XemuSnapshotData *xemu_snapshots_extra_data = NULL;
static int xemu_snapshots_len = 0;
static bool xemu_snapshots_dirty = true;

static bool xemu_snapshots_load_thumbnail(BlockDriverState *bs_ro, XemuSnapshotData *data, int64_t *offset)
{
    int res = bdrv_load_vmstate(bs_ro, (uint8_t*)&data->thumbnail, *offset, sizeof(TextureBuffer) - sizeof(data->thumbnail.buffer));
    if (res != sizeof(TextureBuffer) - sizeof(data->thumbnail.buffer)) return false;
    *offset += res;

    data->thumbnail.buffer = g_malloc(data->thumbnail.size);

    res = bdrv_load_vmstate(bs_ro, (uint8_t*)data->thumbnail.buffer, *offset, data->thumbnail.size);
    if (res != data->thumbnail.size) {
        return false;
    }
    *offset += res;

    return true;
}

static void xemu_snapshots_load_data(BlockDriverState *bs_ro, QEMUSnapshotInfo *info,
                                     XemuSnapshotData *data, Error **err)
{
    int res;
    XemuSnapshotHeader header;
    int64_t offset = 0;

    memset(data, 0, sizeof(XemuSnapshotData));

    data->xbe_title_present = false;
    data->thumbnail_present = false;
    res = bdrv_snapshot_load_tmp(bs_ro, info->id_str, info->name, err);
    if (res < 0) return;

    res = bdrv_load_vmstate(bs_ro, (uint8_t*)&header, offset, sizeof(header));
    if (res != sizeof(header)) goto error;
    offset += res;

    if (header.magic != XEMU_SNAPSHOT_DATA_MAGIC) goto error;

    res = bdrv_load_vmstate(bs_ro, (uint8_t*)&data->xbe_title_len, offset, sizeof(data->xbe_title_len));
    if (res != sizeof(data->xbe_title_len)) goto error;
    offset += res;

    data->xbe_title = (char *) g_malloc(data->xbe_title_len);

    res = bdrv_load_vmstate(bs_ro, (uint8_t*)data->xbe_title, offset, data->xbe_title_len);
    if (res != data->xbe_title_len) goto error;
    offset += res;

    data->xbe_title_present = (offset <= sizeof(header) + header.size);

    if (offset == sizeof(header) + header.size) return;

    if (!xemu_snapshots_load_thumbnail(bs_ro, data, &offset)) {
        goto error;
    }

    data->thumbnail_present = (offset <= sizeof(header) + header.size);
    return;

error:
    g_free(data->xbe_title);
    g_free(data->thumbnail.buffer);
}

static void xemu_snapshots_all_load_data(QEMUSnapshotInfo **info, XemuSnapshotData **data, int snapshots_len, Error **err)
{
    BlockDriverState *bs_ro;
    QDict *opts = qdict_new();

    assert(info && data);

    if (*data) {
        for (int i = 0; i < xemu_snapshots_len; ++i) {
            if((*data)[i].xbe_title_present) {
                g_free((*data)[i].xbe_title);
            }

            if ((*data)[i].thumbnail_present) {
                g_free((*data)[i].thumbnail.buffer);
            }
        }
        g_free(*data);
    }

    *data = (XemuSnapshotData*) g_malloc(sizeof(XemuSnapshotData) * snapshots_len);

    qdict_put_bool(opts, BDRV_OPT_READ_ONLY, true);
    bs_ro = bdrv_open(g_config.sys.files.hdd_path, NULL, opts, BDRV_O_FORCE_RO | BDRV_O_AUTO_RDONLY, err);
    if (!bs_ro) {
        return;
    }

    for (int i = 0; i < snapshots_len; ++i) {
        xemu_snapshots_load_data(bs_ro, (*info) + i, (*data) + i, err);
        if (*err) {
            break;
        }
    }

    bdrv_flush(bs_ro);
    bdrv_drain(bs_ro);
    bdrv_unref(bs_ro);
    assert(bs_ro->refcnt == 0);
    if (!(*err)) xemu_snapshots_dirty = false;
}

int xemu_snapshots_list(QEMUSnapshotInfo **info, XemuSnapshotData **extra_data, Error **err)
{
    BlockDriverState *bs;
    AioContext *aio_context;
    int snapshots_len;
    assert(err);

    if (!xemu_snapshots_dirty && xemu_snapshots_extra_data && xemu_snapshots_metadata) {
        goto done;
    }

    if (xemu_snapshots_metadata) g_free(xemu_snapshots_metadata);

    bs = bdrv_all_find_vmstate_bs(NULL, false, NULL, err);
    if (!bs) {
        return -1;
    }

    aio_context = bdrv_get_aio_context(bs);

    aio_context_acquire(aio_context);
    snapshots_len = bdrv_snapshot_list(bs, &xemu_snapshots_metadata);
    aio_context_release(aio_context);
    xemu_snapshots_all_load_data(&xemu_snapshots_metadata, &xemu_snapshots_extra_data, snapshots_len, err);
    if (*err) {
        return -1;
    }

    xemu_snapshots_len = snapshots_len;

done:
    if (info) {
        *info = xemu_snapshots_metadata;
    }

    if (extra_data) {
        *extra_data = xemu_snapshots_extra_data;
    }

    return xemu_snapshots_len;
}

void xemu_snapshots_load(const char *vm_name, Error **err)
{
    bool vm_running = runstate_is_running();
    vm_stop(RUN_STATE_RESTORE_VM);
    if (load_snapshot(vm_name, NULL, false, NULL, err) && vm_running) {
        vm_start();
    }
}

void xemu_snapshots_save(const char *vm_name, Error **err)
{
    save_snapshot(vm_name, true, NULL, false, NULL, err);
}

void xemu_snapshots_delete(const char *vm_name, Error **err)
{
    delete_snapshot(vm_name, false, NULL, err);
}

void xemu_snapshots_render_thumbnail(unsigned int tex, TextureBuffer *thumbnail)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, thumbnail->width,
                 thumbnail->height, 0, thumbnail->format, thumbnail->type,
                 thumbnail->buffer);
    glBindTexture(GL_TEXTURE_2D, tex);
}

static TextureBuffer *xemu_snapshots_make_thumbnail(void)
{
    /* 
     * Avoids crashing if a snapshot is made on a thread with no GL context
     * Normally, this is not an issue, but it is better to fail safe than assert here.
     */    
    if (!SDL_GL_GetCurrentContext()) {
        return NULL;
    }

    bool flip;
    GLuint tex = sdl2_gl_get_screen_tex(&flip);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    TextureBuffer *tb = (TextureBuffer*) g_malloc(sizeof(TextureBuffer));
    memset(tb, 0, sizeof(TextureBuffer));

    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tb->width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &tb->height);

    tb->format = GL_RGBA;
    tb->type   = GL_UNSIGNED_INT_8_8_8_8;
    tb->size = tb->height * tb->width * 4;

    tb->buffer = g_malloc(tb->size);
    glGetTextureImage(tex, 0, tb->format, tb->type, tb->size, tb->buffer);
    
    int err = glGetError();
    if (err != GL_NO_ERROR) {
        g_free(tb->buffer);
        g_free(tb);
        return NULL;
    }

    if (flip) {
        glo_flip_buffer(4, tb->width * 4, tb->width, tb->height, tb->buffer);
    }

    return tb;
}

void xemu_snapshots_save_extra_data(QEMUFile *f)
{
    struct xbe *xbe_data = xemu_get_xbe_info();

    int64_t xbe_title_len = 0;
    char *xbe_title = g_utf16_to_utf8(xbe_data->cert->m_title_name, 40, NULL, &xbe_title_len, NULL);
    xbe_title_len++;

    XemuSnapshotHeader header = {XEMU_SNAPSHOT_DATA_MAGIC, 0};

    header.size += sizeof(xbe_title_len);
    header.size += xbe_title_len;

    //TextureBuffer *thumbnail = nv2a_get_framebuffer_as_texture_data();
    TextureBuffer *thumbnail = xemu_snapshots_make_thumbnail();
    if (thumbnail && thumbnail->buffer) {
        header.size += sizeof(TextureBuffer) - sizeof(thumbnail->buffer);
        header.size += thumbnail->size;
    }

    qemu_put_buffer(f, (const uint8_t*)&header, sizeof(header));
    qemu_put_buffer(f, (const uint8_t*)&xbe_title_len, sizeof(xbe_title_len));
    qemu_put_buffer(f, (const uint8_t*)xbe_title, xbe_title_len);

    if (thumbnail && thumbnail->buffer) {
        qemu_put_buffer(f, (const uint8_t*)thumbnail, sizeof(TextureBuffer) - sizeof(thumbnail->buffer));
        qemu_put_buffer(f, (const uint8_t*)thumbnail->buffer, thumbnail->size);
    }

    g_free(xbe_title);

    if (thumbnail && thumbnail->buffer) {
        g_free(thumbnail->buffer);
    }

    g_free(thumbnail);

    xemu_snapshots_dirty = true;
}

bool xemu_snapshots_offset_extra_data(QEMUFile *f)
{
    size_t ret;
    XemuSnapshotHeader header;
    ret = qemu_get_buffer(f, (uint8_t*)&header, sizeof(header));
    if (ret != sizeof(header)) {
        return false;
    }

    if (header.magic == XEMU_SNAPSHOT_DATA_MAGIC) {
        /* 
         * qemu_file_skip only works if you aren't skipping past its buffer.
         * Unfortunately, it's not usable here.
         */
        void *buf = g_malloc(header.size);
        qemu_get_buffer(f, buf, header.size);
        g_free(buf);
    } else {
        qemu_file_skip(f, -((int)sizeof(header)));
    }

    return true;
}

void xemu_snapshots_mark_dirty(void)
{
    xemu_snapshots_dirty = true;
}

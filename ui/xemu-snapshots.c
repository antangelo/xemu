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

#include "qemu-common.h"
#include "qapi/error.h"
#include "block/qapi.h"
#include "block/aio.h"
#include "sysemu/runstate.h"
#include "migration/snapshot.h"

int xemu_list_snapshots(QEMUSnapshotInfo **info)
{
    BlockDriverState *bs;
    AioContext *aio_context;
    Error *err = NULL;
    int snapshots_len;

    bs = bdrv_all_find_vmstate_bs(NULL, false, NULL, &err);
    if (!bs) {
        return -1;
    }


    aio_context = bdrv_get_aio_context(bs);

    aio_context_acquire(aio_context);
    snapshots_len = bdrv_snapshot_list(bs, info);
    aio_context_release(aio_context);

    return snapshots_len;
}

void xemu_load_snapshot(const char *vm_name)
{
    Error *err = NULL;
    bool vm_running = runstate_is_running();
    vm_stop(RUN_STATE_RESTORE_VM);
    if (load_snapshot(vm_name, NULL, false, NULL, &err) && vm_running) {
        vm_start();
    }

    if (err) {
        error_reportf_err(err, "loadvm: ");
    }
}

void xemu_save_snapshot(const char *vm_name)
{
    Error *err = NULL;
    save_snapshot(vm_name, true, NULL, false, NULL, &err);
    if (err) {
        error_reportf_err(err, "savevm: ");
    }
}

void xemu_del_snapshot(const char *vm_name)
{
    Error *err = NULL;
    delete_snapshot(vm_name, false, NULL, &err);
    if (err) {
        error_reportf_err(err, "savevm: ");
    }
}

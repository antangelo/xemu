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

#ifndef XEMU_SNAPSHOTS_H
#define XEMU_SNAPSHOTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qemu/osdep.h"
#include "block/snapshot.h"

int xemu_list_snapshots(QEMUSnapshotInfo **info);
void xemu_load_snapshot(const char *vm_name);
void xemu_save_snapshot(const char *vm_name);
void xemu_del_snapshot(const char *vm_name);

#ifdef __cplusplus
}
#endif

#endif

/*
 * The bus model both backends run against: the vector's initial PC, its four
 * instruction words, and the single data access its reference run recorded.
 */

#include <string.h>

#include "core.h"

struct sh2_bus sh2_bus;

void sh2_bus_begin(const struct sh2_test *t)
{
	int i;

	memset(&sh2_bus, 0, sizeof(sh2_bus));
	sh2_bus.base = t->initial.pc;
	memcpy(sh2_bus.opcodes, t->opcodes, sizeof(sh2_bus.opcodes));

	for (i = 0; i < 4; i++) {
		const struct sh2_cycle *c = &t->cyc[i];

		if (c->actions & 1) {
			sh2_bus.have_read = 1;
			sh2_bus.read_addr = c->read_addr;
			sh2_bus.read_val = c->read_val;
			sh2_bus.read_size = c->read_size;
		}
		if (c->actions & 2) {
			sh2_bus.have_write = 1;
			sh2_bus.write_addr = c->write_addr;
			sh2_bus.write_val = c->write_val;
			sh2_bus.write_size = c->write_size;
		}
	}
}

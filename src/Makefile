# Makefile -- Micropolis for SunView (SunOS 4.1)
#
#   make              build with the SunOS /bin/cc (K&R)
#   make CC=gcc       build with gcc
#   make LDFLAGS=-Bstatic
#                     static binary, runs on any SunOS 4.1.x with SunView
#
# Run it from SunView with SIMHOME pointing at the DUX SimCity directory
# (the one with images/, res/ and cities/):
#
#   SIMHOME=/home/SimCity ./micropolis

CC	= cc
OPT	= -O
CFLAGS	= $(OPT) -Iheaders
LDFLAGS	=
LIBS	= -lsuntool -lsunwindow -lpixrect -lm

ENGINE	= g_ani.o rand.o random.o s_alloc.o s_disast.o s_eval.o s_fileio.o \
	  s_gen.o s_init.o s_msg.o s_power.o s_scan.o s_sim.o s_traf.o \
	  s_zone.o w_budget.o w_con.o w_eval.o w_keys.o w_resrc.o \
	  w_sound.o w_sprite.o w_stubs.o w_tool.o w_update.o w_util.o

FRONT	= sv_main.o sv_image.o sv_head.o sv_menu.o sv_editor.o sv_map.o \
	  sv_tools.o sv_dialogs.o sv_graph.o sv_heat.o sv_sound.o sv_stubs.o

OBJS	= $(ENGINE) $(FRONT)

HDRS	= headers/sim.h headers/view.h headers/macros.h headers/mac.h

micropolis: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o micropolis $(OBJS) $(LIBS)

$(ENGINE): $(HDRS)
$(FRONT): $(HDRS) sv.h
g_ani.o: headers/animtab.h

clean:
	rm -f micropolis $(OBJS) core

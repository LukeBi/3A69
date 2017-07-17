FLAGS = -Wall
IMAGES = trace/deleteddirectory.txt trace/deletedfile.txt trace/emptydisk.txt trace/hardlink.txt trace/largefile.txt trace/onedirectory.txt trace/onefile.txt trace/twolevel.txt
ASSIGNMENTFILES = ext2_ls ext2_cp ext2_mkdir ext2_ln ext2_rm ext2_rm_bonus


all : trace imgtrc ${ASSIGNMENTFILES} readimage

readimage : readimage.c
	gcc ${FLAGS} -o $@ $^

ext2_ls : ext2_ls.c
	gcc ${FLAGS} -o $@ $^

ext2_cp :

ext2_mkdir :

ext2_ln :

ext2_rm :

ext2_rm_bonus :

trace :
	mkdir trace

imgtrc : ${IMAGES}

trace/%.txt : %.img
	xxd $<>trace/$*.txt
  
clean: cleantrc cleandumps
	rm -f *.exe

cleandumps:
	rm -f *.stackdump
  
cleantrc:
	rm -f trace/*

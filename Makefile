CC=gcc
CFLAGS=-g
DEFINES=-DHAS_STDINT_H=1

OBJ := main.o localinv.o fileinv.o remoteinv.o ftp.o upload.o md5.o

ftpup : $(OBJ)
	$(CC) $(CFLAGS) -o ftpup $(OBJ)

%.o : %.c
	$(CC) $(CFLAGS) -c $(DEFINES) $< -o $@

# arch-tag: d136d184-baa0-4ea4-8d6f-316463678321


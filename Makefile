CC=gcc
CFLAGS=-g -Wall

OBJ := main.o localinv.o fileinv.o remoteinv.o \
    namecheck.o \
    ftp.o upload.o

ftpup : $(OBJ)
	$(CC) $(CFLAGS) -o ftpup $(OBJ)

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -f *.o ftpup

# arch-tag: d136d184-baa0-4ea4-8d6f-316463678321


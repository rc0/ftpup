CC=gcc
CFLAGS=-O -g -Wall

OBJ := main.o localinv.o fileinv.o remoteinv.o \
    namecheck.o \
    ftp.o upload.o

ftpup : $(OBJ)
	$(CC) $(CFLAGS) -o ftpup $(OBJ)

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -f *.o ftpup


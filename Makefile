CC=gcc
CFLAGS=-g

OBJ := ftpup.o

ftpup : $(OBJ)
	$(CC) $(CFLAGS) -o ftpup $(OBJ)

ftptest : ftp.c
	$(CC) $(CFLAGS) -o ftptest -DTEST ftp.c

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

# arch-tag: d136d184-baa0-4ea4-8d6f-316463678321


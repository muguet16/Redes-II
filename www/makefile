# Variables del compilador
CC = gcc
CFLAGS = -Wall -g

# El objetivo por defecto cuando escribes 'make'
all: server

# Regla para construir el ejecutable final
server: server.o picohttpparser.o
	$(CC) $(CFLAGS) -o server server.o picohttpparser.o

# Reglas para compilar los archivos .c a archivos objeto .o
server.o: server.c picohttpparser.h
	$(CC) $(CFLAGS) -c server.c

picohttpparser.o: picohttpparser.c picohttpparser.h
	$(CC) $(CFLAGS) -c picohttpparser.c

# Regla para limpiar los archivos generados
clean:
	rm -f *.o server
################################################################################
#  Soubor:    Makefile                                                         #
#  Autor:     Radim KUBI�, xkubis03                                            #
#  Vytvo�eno: 5. dubna 2014                                                    #
#                                                                              #
#  Projekt �. 3 do p�edm�tu Pokro�il� opera�n� syst�my (POS).                  #
################################################################################

CC=gcc
CFLAGS=-D_REENTRANT -Wall -g -O -lpthread -std=c99 -pedantic
NAME=proj03

all:
	$(CC) $(CFLAGS) -o $(NAME) $(NAME).c

run:
	./$(NAME)

clean:
	rm -f $(NAME)

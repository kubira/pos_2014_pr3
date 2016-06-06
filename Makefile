################################################################################
#  Soubor:    Makefile                                                         #
#  Autor:     Radim KUBI©, xkubis03                                            #
#  Vytvoøeno: 5. dubna 2014                                                    #
#                                                                              #
#  Projekt è. 3 do pøedmìtu Pokroèilé operaèní systémy (POS).                  #
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

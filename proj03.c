/*
 * Soubor:    proj03.c
 * Autor:     Radim KUBI©, xkubis03
 * Vytvoøeno: 5. dubna 2014
 *
 * Projekt è. 3 do pøedmìtu Pokroèilé operaèní systémy (POS).
 */

#define _XOPEN_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/fcntl.h>

/* STAVY VLÁKEN */
// Stav pro ètecí vlákno
#define READ 0
// Stav pro pracující vlákno
#define WORK 1
// Stav pro ukonèení programu
#define EXIT 2

/* STAVY KONEÈNÉHO AUTOMATU ZPRACOVÁVAJÍCÍHO VSTUP */
// Stav zaèátku vstupu
#define START 0
// Stav pøesmìrování vstupu
#define INPUT 1
// Stav pøesmìrování výstupu
#define OUTPUT 2
// Stav argumentu
#define ARG 3

// Velikost bloku dat pro alokaci
#define BLOCK_SIZE 10
// Maximální délka vstupu
#define MAX_INPUT_SIZE 512

// Promìnná pro aktuální stav vláken
int state = READ;
// Promìnná pro délku aktuálního bufferu
ssize_t inputLength = 0;
// Mutex pro synchronizaci vláken monitorem
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
// Podmínka pro synchronizaci vláken monitorem
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
// Buffer pro vstupní øetìzec
char buffer[(MAX_INPUT_SIZE + 1)] = {0};
// Promìnná pro ulo¾ení pøíznaku, zda bì¾í program na popøedí
int isRunning = 0;

/*
 * Struktura PROGRAM pro spou¹tìný program
 */
typedef struct {
    // Promìnná pro poèet vstupních argumentù programu
    int numberOfArgs;
    // Promìnná pro index aktuálního argumentu programu
    int actualArgIndex;
    // Promìnná pro alokovanou délku aktuálního argumentu programu
    int actualArgLength;
    // Promìnná pro alokovanou délku vstupu
    int inputFileLength;
    // Promìnná pro index vstupu
    int inputFileIndex;
    // Promìnná pro vstup spou¹tìného programu
    char *inputFile;
    // Promìnná pro alokovanou délku výstupu
    int outputFileLength;
    // Promìnná pro index výstupu
    int outputFileIndex;
    // Promìnná pro výstup spou¹tìného programu
    char *outputFile;
    // Pøíznak spou¹tìní na pozadí
    int bgRun;
    // Pole vstupních argumentù
    char **args;
} PROGRAM;

/*
 * Funkce pro zachycený signál SIGCHLD
 *
 * sig - èíslo signálu
 */
void childHandler(int sig) {
    // Promìnná pro návratovou hodnotu funkce waitpid
    pid_t pid = 1;
    // Pokud jsou nìjací potomci
    while (pid > 0) {
        // Pøebírání návratové hodnoty od potomka (neblokované)
        pid = waitpid(-1, NULL, WNOHANG);
    }
}

/*
 * Funkce pro zachycený signál SIGINT
 *
 * sig - èíslo signálu
 */
void sigintHandler(int sig) {
    // Pokud nebì¾í ¾ádný program
    if(isRunning == 0) {
        // Tisk odøádkovaného promptu
        fprintf(stdout, "\n$ ");
    } else if(isRunning == 1) {
        // Pokud bì¾í program

        // Odøádkování výstupu
        fprintf(stdout, "\n");
    }
    // Vyprázdnìní výstupního bufferu
    fflush(stdout);
}

/*
 * Funkce pro inicializaci struktury spou¹tìného programu
 *
 * p - struktura programu
 */
void initStruct(PROGRAM *p) {
    // Aktuální argument je na indexu nula
    p->actualArgIndex = 0;
    // Délka alokované pamìti pro aktuální øetìzec je nula
    p->actualArgLength = 0;
    // Alokace jednoho ukazatele pro øetìzce argumentù
    p->args = malloc(sizeof(char*));
    // Kontrola alokace
    if(p->args == NULL) {
        // Tisk chyby
        perror("initStruct malloc");
        // TODO: exit?
    }
    // Poslední argument je v¾dy NULL
    p->args[0] = NULL;
    // Poèet argumentù programu je 1 -> obsahuje poslední NULL
    p->numberOfArgs = 1;
    // Program nepobì¾í na pozadí
    p->bgRun = 0;
    // Vstupní soubor není nastaven
    p->inputFile = NULL;
    // Index do pole se vstupním souborem je nula
    p->inputFileIndex = 0;
    // Délka alokované pamìti vstupního souboru je nula
    p->inputFileLength = 0;
    // Výstupní soubor není nastaven
    p->outputFile = NULL;
    // Index do pole s výstupním souborem je nula
    p->outputFileIndex = 0;
    // Délka alokované pamìti výstupního souboru je nula
    p->outputFileLength = 0;
}

/*
 * Funkce pro uvolnìní pamìti po struktuøe spou¹tìného programu
 *
 * p - struktura programu
 */
void freeStruct(PROGRAM *p) {
    // Pokud byl zadán vstupní soubor
    if(p->inputFile != NULL) {
        // Uvolnìní pamìti po názvu vstupu
        free(p->inputFile);
    }
    // Pokud byl zadán výstupní soubor
    if(p->outputFile != NULL) {
        // Uvolnìní pamìti po názvy výstupu
        free(p->outputFile);
    }
    // Pokud byly zadány nìjaké argumenty
    if(p->numberOfArgs > 0) {
        // Uvolnìní pamìti po argumentech
        for(int i = 0; i < p->numberOfArgs; i++) {
            // Uvolnìní pamìti po jednom argumentu
            free(p->args[i]);
        }
        // Uvolnìní pamìti po ukazatelech na argumenty
        free(p->args);
    }
}

/*
 * Funkce pro pøidání znaku do øetìzce vstupu
 *
 * p - struktura programu
 * c - pøidávaný znak
 */
void addInputChar(PROGRAM *p, char c) {
    // Pokud není nastaven vstupní soubor
    if(p->inputFileLength == 0) {
        // Alokace místa pro vstupní soubor
        p->inputFile = (char*)malloc(BLOCK_SIZE * sizeof(char));
        // Kontrola alokace
        if(p->inputFile == NULL) {
            // Tisk chyby
            perror("addInputChar malloc");
            // TODO: exit?
        }
        // Nastavení velikosti alokované pamìti na velikost bloku
        p->inputFileLength = BLOCK_SIZE;
    } else if(p->inputFileLength == p->inputFileIndex) {
        // Pokud není dost místa pro nový znak

        // Zvý¹ení velikosti alokované pamìti
        p->inputFileLength += BLOCK_SIZE;
        // Realokace pamìti pro dal¹í znaky
        p->inputFile = (char*)realloc(p->inputFile, p->inputFileLength * sizeof(char));
        // Kontrola realokace
        if(p->inputFile == NULL) {
            // Tisk chyby
            perror("addInputChar realloc");
            // TODO: exit?
        }
    }
    // Ulo¾ení nového znaku øetìzce
    p->inputFile[p->inputFileIndex] = c;
    // Inkrementace indexu dal¹ího znaku øetìzce
    p->inputFileIndex++;
}

/*
 * Funkce pro pøidání znaku do øetìzce výstupu
 *
 * p - struktura programu
 * c - pøidávaný znak
 */
void addOutputChar(PROGRAM *p, char c) {
    // Pokud není nastaven výstupní soubor
    if(p->outputFileLength == 0) {
        // Alokace místa pro výstupní soubor
        p->outputFile = (char*)malloc(BLOCK_SIZE * sizeof(char));
        // Kontrola alokace
        if(p->outputFile == NULL) {
            // Tisk chyby
            perror("addOutputChar malloc");
            // TODO: exit?
        }
        // Nastavení velikosti alokované pamìti na velikost bloku
        p->outputFileLength = BLOCK_SIZE;
    } else if(p->outputFileLength == p->outputFileIndex) {
        // Pokud není dost místa pro nový znak

        // Zvý¹ení velikosti alokované pamìti
        p->outputFileLength += BLOCK_SIZE;
        // Realokace pamìti pro dal¹í znaky
        p->outputFile = (char*)realloc(p->outputFile, p->outputFileLength * sizeof(char));
        // Kontrola realokace
        if(p->outputFile == NULL) {
            // Tisk chyby
            perror("addOutputChar realloc");
            // TODO: exit?
        }
    }
    // Ulo¾ení nového znaku øetìzce
    p->outputFile[p->outputFileIndex] = c;
    // Inkrementace indexu dal¹ího znaku øetìzce
    p->outputFileIndex++;
}

/*
 * Funkce pro inicializaci nového argumentu
 *
 * p - struktura programu
 * c - první znak argumentu
 */
void newArg(PROGRAM *p, char c) {
    // Zvý¹ení poètu pøedávaných argumentù
    p->numberOfArgs++;
    // Realokace pole s argumenty o dal¹í ukazatel
    p->args = realloc(p->args, (p->numberOfArgs * sizeof(char*)));
    // Kontrola realokace
    if(p->args == NULL) {
        // Tisk chyby
        perror("newArg realloc");
        // TODO: exit?
    }
    // Alokace místa pro nový argument
    p->args[p->numberOfArgs-2] = (char*)malloc(BLOCK_SIZE * sizeof(char));
    // Kontrola alokace
    if(p->args[p->numberOfArgs-2] == NULL) {
        // Tisk chyby
        perror("newArg malloc");
        // TODO: exit?
    }
    // Poslední argument je v¾dy NULL
    p->args[p->numberOfArgs-1] = NULL;
    // Ulo¾ení nového znaku do øetìzce
    p->args[p->numberOfArgs-2][0] = c;
    // Nastavení indexu dal¹ího znaku na 1
    p->actualArgIndex = 1;
    // Nastavení alokované pamìti na velikost jednoho bloku
    p->actualArgLength = BLOCK_SIZE;
}

/*
 * Funkce pro pøidání znaku do øetìzce argumentu
 *
 * p - struktura programu
 * c - pøidávaný znak
 */
void addArgChar(PROGRAM *p, char c) {
    // Pokud není v øetìzci dost místa na dal¹í znaky
    if(p->actualArgLength == p->actualArgIndex) {
        // Zvý¹ení velikosti místa øetìzce
        p->actualArgLength += BLOCK_SIZE;
        // Realokace místa pro øetìzec
        p->args[p->numberOfArgs-2] = (char*)realloc(p->args[p->numberOfArgs-2], p->actualArgLength * sizeof(char));
        // Kontrola realokace
        if(p->args[p->numberOfArgs-2] == NULL) {
            // Tisk chyby
            perror("addArgChar realloc");
            // TODO: exit?
        }
    }
    // Ulo¾ení znaku do øetìzce
    p->args[p->numberOfArgs-2][p->actualArgIndex] = c;
    // Inkrementace poètu naètených znakù v øetìzci
    p->actualArgIndex++;
}

/*
 * Funkce pro nastavení spou¹tìní na pozadí
 *
 * p     - struktura programu
 * value - hodnota pro nastavení
 */
void setBgRun(PROGRAM *p, int value) {
    // Spou¹tìní na pozadí se nastaví na hodnotu value
    p->bgRun = value;
}

/*
 * Funkce s obsahem programu pro ètecí vlákno
 *
 * arg - argument posílaný z funkce pthread_create
 */
void *readingThread(void *arg) {
    // Promìnná pro návratové hodnoty funkcí
    int result = 0;
    // Hlavní smyèka vlákna, stále ète ze vstupu
    while(state != EXIT) {
        // Uzamèení mutexu
        result = pthread_mutex_lock(&mutex);
        // Kontrola uzamèení mutexu
        if(result != 0) {
            // Tisk chyby
            perror("readingThread pthread_mutex_lock");
            // TODO: exit?
        }

        // Èekání, dokud není povoleno èíst
        while(state != READ) {
            // Èekání na zmìnu podmínky
            result = pthread_cond_wait(&cond, &mutex);
            // Kontrola èekání na podmínku
            if(result != 0) {
                // Tisk chyby
                perror("readingThread pthread_cond_wait");
                // TODO: exit?
            }
        }

        // Výpis promptu shellu
        fprintf(stdout, "$ ");
        // Vyprázdnìní výstupního bufferu
        fflush(stdout);

        // Naètení maximální povolené délky vstupu
        inputLength = read(fileno(stdin), buffer, (MAX_INPUT_SIZE + 1));
        // Kontrola naètení vstupu
        if(inputLength == -1) {
            // Tisk chyby
            perror("readingThread read");
            // TODO: exit?
        } else {
            // Pokud je poslední naètený znak konec øádku,
            // byl naèten celý vstup (maximálnì 512 znakù)
            // a 513. znak je právì konec øádku
            if(buffer[inputLength-1] == '\n') {
                // Ukonèení naèteného vstupu koncem øetìzce
                buffer[inputLength-1] = '\0';

                // Kontrola, zda nebyl zadán pøíkaz exit
                if(strncmp("exit", buffer, 4) == 0) {
                    // Nastavení stavu programu na konec shellu
                    state = EXIT;
                } else {
                    // Zmìna stavu programu na zpracování vstupu
                    state = WORK;
                }

                // Signalizace podmínky zmìny stavu programu
                result = pthread_cond_signal(&cond);
                // Kontrola signalizace podmínky
                if(result != 0) {
                    // Tisk chyby
                    perror("readingThread pthread_cond_signal");
                    // TODO: exit?
                }
            } else {
                // Pokud poslední naètený znak není konec øádku,
                // vstup byl del¹í ne¾ 512 znakù, co¾ není povoleno

                // Tisk chyby
                fprintf(stderr, "Chyba: Pøíli¹ dlouhý vstup.\n");

                // Cyklus pro naètení celého zbytku vstupu
                while(inputLength > 0) {
                    // Naètení maximálnì 513 znakù ze vstupu do bufferu
                    inputLength = read(fileno(stdin), buffer, (MAX_INPUT_SIZE + 1));
                    // Kontrola naètení vstupu
                    if(inputLength == -1) {
                        // Tisk chyby
                        perror("readingThread: too long input read");
                        // Ukonèení cyklu
                        break;
                    }
                }
            }
        }

        // Uvolnìní mutexu
        result = pthread_mutex_unlock(&mutex);
        // Kontrola uvolnìní mutexu
        if(result != 0) {
            // Tisk chyby
            perror("readingThread pthread_mutex_unlock");
            // TODO: exit?
        }
    }

    // Ukonèení vlákna bez chyby
    exit(EXIT_SUCCESS);
}

/*
 * Funkce s obsahem programu pro pracující vlákno
 *
 * arg - argument posílaný z funkce pthread_create
 */
void *workingThread(void *arg) {
    // Promìnná pro stav KA zpracovávání vstupu
    int KAState = START;
    // Promìnná pro návratové hodnoty funkcí
    int result = 0;
    // Hlavní smyèka vlákna, stále zpracovává buffer
    while(state != EXIT) {
        // Nastavení stavu KA na nový vstup
        KAState = START;
        // Uzamèení mutexu
        result = pthread_mutex_lock(&mutex);
        // Kontrola uzamèení mutexu
        if(result != 0) {
            // Tisk chyby
            perror("workingThread pthread_mutex_lock");
            // TODO: exit?
        }
        // Èekání, dokud není povoleno zpracovávat
        while(state != WORK) {
            // Èekání na zmìnu podmínky
            result = pthread_cond_wait(&cond, &mutex);
            // Kontrola èekání na podmínku
            if(result != 0) {
                // Tisk chyby
                perror("workingThread pthread_cond_wait");
                // TODO: exit?
            }

            // Pokud je stav EXIT
            if(state == EXIT) {
                // Konec vlákna
                exit(EXIT_SUCCESS);
            }
        }

        // Struktura pro novì spou¹tìný program
        PROGRAM p;
        // Inicializace struktury pro program
        initStruct(&p);

        // Zpracování vstupu po znacích koneèným automatem
        for(int i = 0; i < inputLength; i++) {
            // Roznodnutí pro vìtev KA
            switch(KAState) {
                // Zaèátek vstupu nebo nového argumentu
                case START: {
                    // Pøeskoèení mezery nebo konce vstupu
                    if(buffer[i] == ' ' || buffer[i] == '\0') {
                        // Skok na dal¹í iteraci
                        continue;
                    } else if(buffer[i] == '<') {
                        // Pokud je na vstupu znak pøesmìrování vstupu,
                        // automat bude ve stavu naèítání vstupního souboru
                        KAState = INPUT;
                    } else if(buffer[i] == '>') {
                        // Pokud je na vstupu znak pøesmìrování výstupu,
                        // automat bude ve stavu naèítání výstupního souboru
                        KAState = OUTPUT;
                    } else if(buffer[i] == '&') {
                        // Pokud je na vstupu znak bìhu na pozadí,
                        // nastaví se bìh na pozadí spou¹tìného programu
                        setBgRun(&p, 1);
                    } else {
                        // Pokud je na vstupu nìco jiného,
                        // jedná se o argument spou¹tìného programu.

                        // Vytvoøí se nový argument programu
                        newArg(&p, buffer[i]);
                        // Automat bude ve stavu naèítání argumentu
                        KAState = ARG;
                    }
                    // Konec vìtve vstupu nebo nového argumentu
                    break;
                }
                // Naèítání vstupního souboru
                case INPUT: {
                    // Pokud je na vstupu mezera nebo konec vstupu
                    if(buffer[i] == ' ' || buffer[i] == '\0') {
                        // Konec názvu vstupního souboru
                        addInputChar(&p, '\0');
                        // Nastavení stavu na nový zaèátek
                        KAState = START;
                    } else {
                        // Jinak se do názvu vstupního souboru vlo¾í dal¹í znak
                        addInputChar(&p, buffer[i]);
                    }
                    // Konec vìtve naèítání vstupního souboru
                    break;
                }
                // Naèítání výstupního souboru
                case OUTPUT: {
                    // Pokud je na vstupu mezera nebo konec vstupu
                    if(buffer[i] == ' ' || buffer[i] == '\0') {
                        // Konec názvu výstupního souboru
                        addOutputChar(&p, '\0');
                        // Nastavení stavu na nový zaèátek
                        KAState = START;
                    } else {
                        // Jinak se do názvu výstupního souboru vlo¾í dal¹í znak
                        addOutputChar(&p, buffer[i]);
                    }
                    // Konec vìtve naèítání výstupního souboru
                    break;
                }
                // Naèítání argumentu programu
                case ARG: {
                    // Pokud je na vstupu mezera nebo konec vstupu
                    if(buffer[i] == ' ' || buffer[i] == '\0') {
                        // Konec argumentu programu
                        addArgChar(&p, '\0');
                        // Nastavení stavu na nový zaèátek
                        KAState = START;
                    } else {
                        // Jinak se do argumentu vlo¾í dal¹í znak
                        addArgChar(&p, buffer[i]);
                    }
                    // Konec vìtve naèítání argumentu
                    break;
                }
            }
        }

        // Vytvoøení podprocesu pro spu¹tìní zadaného programu
        pid_t pid = fork();

        // ROZVÌTVENÍ PROCESÙ
        // Pokud nastala chyba pøi vytvoøení nového procesu
        if(pid == -1) {
            // Tisk chyby
            perror("fork");
        } else if(pid == 0) {
            // POKUD SE JEDNÁ O NOVÝ PROCES PRO SPU©TÌNÍ

            // Promìnná pro návratové hodnoty funkcí
            int resultChild = 0;
            // Pokud má proces bì¾et na pozadí
            if(p.bgRun == 1) {
                // Seznam signálù k blokování
                sigset_t sigintSet;
                // Vyprázdnìní seznamu signálù
                resultChild = sigemptyset(&sigintSet);
                // Kontrola vyprázdnìní seznamu
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("sigemptyset");
                }
                // Pøidání signálu SIGINT do seznamu
                resultChild = sigaddset(&sigintSet, SIGINT);
                // Kontrola pøidání signálu do seznamu
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("sigaddset");
                }
                // Blokování seznamu signálù
                resultChild = sigprocmask(SIG_BLOCK, &sigintSet, NULL);
                // Kontrola nastavení blokování seznamu
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("sigprocmask");
                }
            }

            // Pokud byl zadán vstupní soubor
            if(p.inputFileLength > 0) {
                // Otevøení vstupního souboru pro ètení
                int inputFile = open(p.inputFile, O_RDONLY);
                // Kontrola otevøení souboru
                if(inputFile == -1) {
                    // Tisk chyby
                    perror("open inputFile");
                    // Ukonèení procesu s chybou
                    exit(EXIT_FAILURE);
                }
                // Uzavøení standardního vstupu
                resultChild = close(0);
                // Kontrola uzavøení
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("close input");
                    // Ukonèení procesu s chybou
                    exit(EXIT_FAILURE);
                }
                // Duplikace otevøeného souboru pro vstup
                resultChild = dup(inputFile);
                // Kontrola duplikace
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("dup inputFile");
                    // Ukonèení procesu s chybou
                    exit(EXIT_FAILURE);
                }
                // Uzavøení otevøeného souboru
                resultChild = close(inputFile);
                // Kontrola uzavøení
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("close inputFile");
                    // Ukonèení procesu s chybou
                    exit(EXIT_FAILURE);
                }
            }

            // Pokud byl zadán výstupní soubor
            if(p.outputFileLength > 0) {
                // Nastavení pøíznakù pro zápis a vytvoøení výstupního souboru
                int flags = O_WRONLY | O_CREAT;
                // Nastavení módu pøístupových práv k souboru
                mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
                // Otevøení výstupního souboru s pøíznaky a právy
                int outputFile = open(p.outputFile, flags, mode);
                // Kontrola otevøení výstupu
                if(outputFile == -1) {
                    // Tisk chyby
                    perror("open outputFile");
                    // Ukonèení procesu s chybou
                    exit(EXIT_FAILURE);
                }
                // Uzavøení standardního výstupu
                resultChild = close(1);
                // Kontrola uzavøení výstupu
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("close output");
                    // Ukonèení procesu s chybou
                    exit(EXIT_FAILURE);
                }
                // Duplikace otevøeného souboru pro výstup
                resultChild = dup(outputFile);
                // Kontrola duplikace
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("dup outputFile");
                    // Ukonèení procesu s chybou
                    exit(EXIT_FAILURE);
                }
                // Uzavøení otevøeného souboru
                close(outputFile);
                // Kontrola uzavøení
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("close outputFile");
                    // Ukonèení procesu s chybou
                    exit(EXIT_FAILURE);
                }
            }
            // Spu¹tìní zadaného programu s jeho argumenty
            execvp(p.args[0], p.args);
            // SEM SE KOREKTNÌ SPU©TÌNÝ NOVÝ PROCES NEMÙ®E DOSTAT

            // Výpis chyby, pokud se nepodaøilo spustit program
            perror(p.args[0]);
            // Ukonèení podprocesu s chybou
            exit(EXIT_FAILURE);
        } else {
            // POKUD SE JEDNÁ O RODIÈOVSKÝ PROCES

            // Pokud není nastaven pøíznak bìhu na pozadí
            if(p.bgRun == 0) {
                // Nastavení pøíznaku bì¾ícího programu na popøedí
                isRunning = 1;
                // Èekání na ukonèení spu¹tìného programu
                while(waitpid(pid, NULL, 0) != pid) {
                    // Pokud bylo èekání pøeru¹eno
                    if(errno == EINTR) {
                        // Pokraèuj v èekání
                        continue;
                    } else if(errno == ECHILD) {
                        // Pokud není potomek, na kterého lze èekat,
                        // ukonèi èekání
                        break;
                    } else {
                        // Pokud nastala jiná chyba,
                        // tisk chyby
                        perror("parent waitpid");
                        // Konec èekání
                        break;
                    }
                }
                // Nastavení pøíznaku nebì¾ícího programu na popøedí
                isRunning = 0;
            }
        }

        // Uvolnìní pamìti po struktuøe programu
        freeStruct(&p);
        // Nastavení bufferu na prázdný øetìzec
        buffer[0] = '\0';
        // Zmìna stavu programu na ètení
        state = READ;
        // Signalizace zmìny podmínky
        result = pthread_cond_signal(&cond);
        // Kontrola signalizace podmínky
        if(result != 0) {
            // Tisk chyby
            perror("workingThread pthread_cond_signal");
            // TODO: exit?
        }
        // Uvolnìní mutexu
        result = pthread_mutex_unlock(&mutex);
        // Kontrola uvolnìní mutexu
        if(result != 0) {
            // Tisk chyby
            perror("workingThread pthread_mutex_unlock");
            // TODO: exit?
        }
    }

    // Ukonèení vlákna bez chyby
    exit(EXIT_SUCCESS);
}

/*
 * Hlavní funkce main.
 *
 * argc - poèet argumentù pøíkazového øádku
 * argv - argumenty pøíkazové øádky
 *
 * Návratová hodnota:
 *         0 - program probìhl v poøádku
 *     jinak - program skonèil s chybou
 */
int main(int argc, char *argv[]) {
    // Promìnná pro ulo¾ení návratové hodnoty programu
    int returnValue = EXIT_SUCCESS;
    // Promìnná pro návratové hodnoty funkcí
    int result = 0;
    // Promìnná pro ètecí vlákno
    pthread_t reader;
    // Promìnná pro pracující vlákno
    pthread_t worker;
    // Struktura pro nastavení reakce na signál od potomkù
    struct sigaction childAct;
    // Struktura pro nastavení reakce na signál pøeru¹ení
    struct sigaction sigintAct;

    // Vynulování struktury childAct
    memset(&childAct, 0, sizeof(childAct));
    // Vynulování struktury sigintAct
    memset(&sigintAct, 0, sizeof(sigintAct));
    // Nastavení funkce pro reakci na signál od potomkù
    childAct.sa_handler = childHandler;
    // Nastavení funkce pro reakci na signál pøeru¹ení
    sigintAct.sa_handler = sigintHandler;

    // Pøiøazení funkce k signálu od potomkù
    if(sigaction(SIGCHLD, &childAct, NULL)) {
        // Tisk chyby
        perror("sigaction childAct");
        // Ukonèení programu s chybou
        exit(EXIT_FAILURE);
    }
    // Pøiøazení funkce k signálu pøeru¹ení
    if(sigaction(SIGINT, &sigintAct, NULL)) {
        // Tisk chyby
        perror("sigaction sigintAct");
        // Ukonèení programu s chybou
        exit(EXIT_FAILURE);
    }

    // Vytvoøení ètecího vlákna
    result = pthread_create(&reader, NULL, readingThread, NULL);
    // Kontrola vytvoøení vlákna
    if(result != 0) {
        // Tisk chyby
        perror("pthread_create reader");
        // Ukonèení programu s chybou
        exit(EXIT_FAILURE);
    }
    // Vytvoøení pracujícího vlákna
    result = pthread_create(&worker, NULL, workingThread, NULL);
    // Kontrola vytvoøení vlákna
    if(result != 0) {
        // Tisk chyby
        perror("pthread_create worker");
        // Zru¹ení ètecího vlákna
        result = pthread_cancel(reader);
        // Kontrola zru¹ení ètecího vlákna
        if(result != 0) {
            // Tisk chyby
            perror("pthread_cancel reader");
        } else {
            // Èekání na ukonèení zru¹eného vlákna
            result = pthread_join(reader, NULL);
            // Kontrola èekání na ukonèení zru¹eného vlákna
            if(result != 0) {
                // Tisk chyby
                perror("pthread_join reader");
            }
        }
        // Ukonèení programu s chybou
        exit(EXIT_FAILURE);
    }

    // Èekání na ukonèení ètecího vlákna
    result = pthread_join(reader, NULL);
    // Kontrola ukonèení ètecího vlákna
    if(result != 0) {
        // Tisk chyby
        perror("pthread_join reader");
        // Nastavení návratové hodnoty na chybovou
        returnValue = EXIT_FAILURE;
    }
    // Èekání na ukonèení pracujícího vlákna
    result = pthread_join(worker, NULL);
    // Kontrola ukonèení pracujícího vlákna
    if(result != 0) {
        // Tisk chyby
        perror("pthread_join worker");
        // Ukonèení programu s chybou
        exit(EXIT_FAILURE);
    }

    // Ukonèení programu podle nastavené návratové hodnoty
    exit(returnValue);
}

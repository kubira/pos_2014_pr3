/*
 * Soubor:    proj03.c
 * Autor:     Radim KUBI�, xkubis03
 * Vytvo�eno: 5. dubna 2014
 *
 * Projekt �. 3 do p�edm�tu Pokro�il� opera�n� syst�my (POS).
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

/* STAVY VL�KEN */
// Stav pro �tec� vl�kno
#define READ 0
// Stav pro pracuj�c� vl�kno
#define WORK 1
// Stav pro ukon�en� programu
#define EXIT 2

/* STAVY KONE�N�HO AUTOMATU ZPRACOV�VAJ�C�HO VSTUP */
// Stav za��tku vstupu
#define START 0
// Stav p�esm�rov�n� vstupu
#define INPUT 1
// Stav p�esm�rov�n� v�stupu
#define OUTPUT 2
// Stav argumentu
#define ARG 3

// Velikost bloku dat pro alokaci
#define BLOCK_SIZE 10
// Maxim�ln� d�lka vstupu
#define MAX_INPUT_SIZE 512

// Prom�nn� pro aktu�ln� stav vl�ken
int state = READ;
// Prom�nn� pro d�lku aktu�ln�ho bufferu
ssize_t inputLength = 0;
// Mutex pro synchronizaci vl�ken monitorem
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
// Podm�nka pro synchronizaci vl�ken monitorem
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
// Buffer pro vstupn� �et�zec
char buffer[(MAX_INPUT_SIZE + 1)] = {0};
// Prom�nn� pro ulo�en� p��znaku, zda b�� program na pop�ed�
int isRunning = 0;

/*
 * Struktura PROGRAM pro spou�t�n� program
 */
typedef struct {
    // Prom�nn� pro po�et vstupn�ch argument� programu
    int numberOfArgs;
    // Prom�nn� pro index aktu�ln�ho argumentu programu
    int actualArgIndex;
    // Prom�nn� pro alokovanou d�lku aktu�ln�ho argumentu programu
    int actualArgLength;
    // Prom�nn� pro alokovanou d�lku vstupu
    int inputFileLength;
    // Prom�nn� pro index vstupu
    int inputFileIndex;
    // Prom�nn� pro vstup spou�t�n�ho programu
    char *inputFile;
    // Prom�nn� pro alokovanou d�lku v�stupu
    int outputFileLength;
    // Prom�nn� pro index v�stupu
    int outputFileIndex;
    // Prom�nn� pro v�stup spou�t�n�ho programu
    char *outputFile;
    // P��znak spou�t�n� na pozad�
    int bgRun;
    // Pole vstupn�ch argument�
    char **args;
} PROGRAM;

/*
 * Funkce pro zachycen� sign�l SIGCHLD
 *
 * sig - ��slo sign�lu
 */
void childHandler(int sig) {
    // Prom�nn� pro n�vratovou hodnotu funkce waitpid
    pid_t pid = 1;
    // Pokud jsou n�jac� potomci
    while (pid > 0) {
        // P�eb�r�n� n�vratov� hodnoty od potomka (neblokovan�)
        pid = waitpid(-1, NULL, WNOHANG);
    }
}

/*
 * Funkce pro zachycen� sign�l SIGINT
 *
 * sig - ��slo sign�lu
 */
void sigintHandler(int sig) {
    // Pokud neb�� ��dn� program
    if(isRunning == 0) {
        // Tisk od��dkovan�ho promptu
        fprintf(stdout, "\n$ ");
    } else if(isRunning == 1) {
        // Pokud b�� program

        // Od��dkov�n� v�stupu
        fprintf(stdout, "\n");
    }
    // Vypr�zdn�n� v�stupn�ho bufferu
    fflush(stdout);
}

/*
 * Funkce pro inicializaci struktury spou�t�n�ho programu
 *
 * p - struktura programu
 */
void initStruct(PROGRAM *p) {
    // Aktu�ln� argument je na indexu nula
    p->actualArgIndex = 0;
    // D�lka alokovan� pam�ti pro aktu�ln� �et�zec je nula
    p->actualArgLength = 0;
    // Alokace jednoho ukazatele pro �et�zce argument�
    p->args = malloc(sizeof(char*));
    // Kontrola alokace
    if(p->args == NULL) {
        // Tisk chyby
        perror("initStruct malloc");
        // TODO: exit?
    }
    // Posledn� argument je v�dy NULL
    p->args[0] = NULL;
    // Po�et argument� programu je 1 -> obsahuje posledn� NULL
    p->numberOfArgs = 1;
    // Program nepob�� na pozad�
    p->bgRun = 0;
    // Vstupn� soubor nen� nastaven
    p->inputFile = NULL;
    // Index do pole se vstupn�m souborem je nula
    p->inputFileIndex = 0;
    // D�lka alokovan� pam�ti vstupn�ho souboru je nula
    p->inputFileLength = 0;
    // V�stupn� soubor nen� nastaven
    p->outputFile = NULL;
    // Index do pole s v�stupn�m souborem je nula
    p->outputFileIndex = 0;
    // D�lka alokovan� pam�ti v�stupn�ho souboru je nula
    p->outputFileLength = 0;
}

/*
 * Funkce pro uvoln�n� pam�ti po struktu�e spou�t�n�ho programu
 *
 * p - struktura programu
 */
void freeStruct(PROGRAM *p) {
    // Pokud byl zad�n vstupn� soubor
    if(p->inputFile != NULL) {
        // Uvoln�n� pam�ti po n�zvu vstupu
        free(p->inputFile);
    }
    // Pokud byl zad�n v�stupn� soubor
    if(p->outputFile != NULL) {
        // Uvoln�n� pam�ti po n�zvy v�stupu
        free(p->outputFile);
    }
    // Pokud byly zad�ny n�jak� argumenty
    if(p->numberOfArgs > 0) {
        // Uvoln�n� pam�ti po argumentech
        for(int i = 0; i < p->numberOfArgs; i++) {
            // Uvoln�n� pam�ti po jednom argumentu
            free(p->args[i]);
        }
        // Uvoln�n� pam�ti po ukazatelech na argumenty
        free(p->args);
    }
}

/*
 * Funkce pro p�id�n� znaku do �et�zce vstupu
 *
 * p - struktura programu
 * c - p�id�van� znak
 */
void addInputChar(PROGRAM *p, char c) {
    // Pokud nen� nastaven vstupn� soubor
    if(p->inputFileLength == 0) {
        // Alokace m�sta pro vstupn� soubor
        p->inputFile = (char*)malloc(BLOCK_SIZE * sizeof(char));
        // Kontrola alokace
        if(p->inputFile == NULL) {
            // Tisk chyby
            perror("addInputChar malloc");
            // TODO: exit?
        }
        // Nastaven� velikosti alokovan� pam�ti na velikost bloku
        p->inputFileLength = BLOCK_SIZE;
    } else if(p->inputFileLength == p->inputFileIndex) {
        // Pokud nen� dost m�sta pro nov� znak

        // Zv��en� velikosti alokovan� pam�ti
        p->inputFileLength += BLOCK_SIZE;
        // Realokace pam�ti pro dal�� znaky
        p->inputFile = (char*)realloc(p->inputFile, p->inputFileLength * sizeof(char));
        // Kontrola realokace
        if(p->inputFile == NULL) {
            // Tisk chyby
            perror("addInputChar realloc");
            // TODO: exit?
        }
    }
    // Ulo�en� nov�ho znaku �et�zce
    p->inputFile[p->inputFileIndex] = c;
    // Inkrementace indexu dal��ho znaku �et�zce
    p->inputFileIndex++;
}

/*
 * Funkce pro p�id�n� znaku do �et�zce v�stupu
 *
 * p - struktura programu
 * c - p�id�van� znak
 */
void addOutputChar(PROGRAM *p, char c) {
    // Pokud nen� nastaven v�stupn� soubor
    if(p->outputFileLength == 0) {
        // Alokace m�sta pro v�stupn� soubor
        p->outputFile = (char*)malloc(BLOCK_SIZE * sizeof(char));
        // Kontrola alokace
        if(p->outputFile == NULL) {
            // Tisk chyby
            perror("addOutputChar malloc");
            // TODO: exit?
        }
        // Nastaven� velikosti alokovan� pam�ti na velikost bloku
        p->outputFileLength = BLOCK_SIZE;
    } else if(p->outputFileLength == p->outputFileIndex) {
        // Pokud nen� dost m�sta pro nov� znak

        // Zv��en� velikosti alokovan� pam�ti
        p->outputFileLength += BLOCK_SIZE;
        // Realokace pam�ti pro dal�� znaky
        p->outputFile = (char*)realloc(p->outputFile, p->outputFileLength * sizeof(char));
        // Kontrola realokace
        if(p->outputFile == NULL) {
            // Tisk chyby
            perror("addOutputChar realloc");
            // TODO: exit?
        }
    }
    // Ulo�en� nov�ho znaku �et�zce
    p->outputFile[p->outputFileIndex] = c;
    // Inkrementace indexu dal��ho znaku �et�zce
    p->outputFileIndex++;
}

/*
 * Funkce pro inicializaci nov�ho argumentu
 *
 * p - struktura programu
 * c - prvn� znak argumentu
 */
void newArg(PROGRAM *p, char c) {
    // Zv��en� po�tu p�ed�van�ch argument�
    p->numberOfArgs++;
    // Realokace pole s argumenty o dal�� ukazatel
    p->args = realloc(p->args, (p->numberOfArgs * sizeof(char*)));
    // Kontrola realokace
    if(p->args == NULL) {
        // Tisk chyby
        perror("newArg realloc");
        // TODO: exit?
    }
    // Alokace m�sta pro nov� argument
    p->args[p->numberOfArgs-2] = (char*)malloc(BLOCK_SIZE * sizeof(char));
    // Kontrola alokace
    if(p->args[p->numberOfArgs-2] == NULL) {
        // Tisk chyby
        perror("newArg malloc");
        // TODO: exit?
    }
    // Posledn� argument je v�dy NULL
    p->args[p->numberOfArgs-1] = NULL;
    // Ulo�en� nov�ho znaku do �et�zce
    p->args[p->numberOfArgs-2][0] = c;
    // Nastaven� indexu dal��ho znaku na 1
    p->actualArgIndex = 1;
    // Nastaven� alokovan� pam�ti na velikost jednoho bloku
    p->actualArgLength = BLOCK_SIZE;
}

/*
 * Funkce pro p�id�n� znaku do �et�zce argumentu
 *
 * p - struktura programu
 * c - p�id�van� znak
 */
void addArgChar(PROGRAM *p, char c) {
    // Pokud nen� v �et�zci dost m�sta na dal�� znaky
    if(p->actualArgLength == p->actualArgIndex) {
        // Zv��en� velikosti m�sta �et�zce
        p->actualArgLength += BLOCK_SIZE;
        // Realokace m�sta pro �et�zec
        p->args[p->numberOfArgs-2] = (char*)realloc(p->args[p->numberOfArgs-2], p->actualArgLength * sizeof(char));
        // Kontrola realokace
        if(p->args[p->numberOfArgs-2] == NULL) {
            // Tisk chyby
            perror("addArgChar realloc");
            // TODO: exit?
        }
    }
    // Ulo�en� znaku do �et�zce
    p->args[p->numberOfArgs-2][p->actualArgIndex] = c;
    // Inkrementace po�tu na�ten�ch znak� v �et�zci
    p->actualArgIndex++;
}

/*
 * Funkce pro nastaven� spou�t�n� na pozad�
 *
 * p     - struktura programu
 * value - hodnota pro nastaven�
 */
void setBgRun(PROGRAM *p, int value) {
    // Spou�t�n� na pozad� se nastav� na hodnotu value
    p->bgRun = value;
}

/*
 * Funkce s obsahem programu pro �tec� vl�kno
 *
 * arg - argument pos�lan� z funkce pthread_create
 */
void *readingThread(void *arg) {
    // Prom�nn� pro n�vratov� hodnoty funkc�
    int result = 0;
    // Hlavn� smy�ka vl�kna, st�le �te ze vstupu
    while(state != EXIT) {
        // Uzam�en� mutexu
        result = pthread_mutex_lock(&mutex);
        // Kontrola uzam�en� mutexu
        if(result != 0) {
            // Tisk chyby
            perror("readingThread pthread_mutex_lock");
            // TODO: exit?
        }

        // �ek�n�, dokud nen� povoleno ��st
        while(state != READ) {
            // �ek�n� na zm�nu podm�nky
            result = pthread_cond_wait(&cond, &mutex);
            // Kontrola �ek�n� na podm�nku
            if(result != 0) {
                // Tisk chyby
                perror("readingThread pthread_cond_wait");
                // TODO: exit?
            }
        }

        // V�pis promptu shellu
        fprintf(stdout, "$ ");
        // Vypr�zdn�n� v�stupn�ho bufferu
        fflush(stdout);

        // Na�ten� maxim�ln� povolen� d�lky vstupu
        inputLength = read(fileno(stdin), buffer, (MAX_INPUT_SIZE + 1));
        // Kontrola na�ten� vstupu
        if(inputLength == -1) {
            // Tisk chyby
            perror("readingThread read");
            // TODO: exit?
        } else {
            // Pokud je posledn� na�ten� znak konec ��dku,
            // byl na�ten cel� vstup (maxim�ln� 512 znak�)
            // a 513. znak je pr�v� konec ��dku
            if(buffer[inputLength-1] == '\n') {
                // Ukon�en� na�ten�ho vstupu koncem �et�zce
                buffer[inputLength-1] = '\0';

                // Kontrola, zda nebyl zad�n p��kaz exit
                if(strncmp("exit", buffer, 4) == 0) {
                    // Nastaven� stavu programu na konec shellu
                    state = EXIT;
                } else {
                    // Zm�na stavu programu na zpracov�n� vstupu
                    state = WORK;
                }

                // Signalizace podm�nky zm�ny stavu programu
                result = pthread_cond_signal(&cond);
                // Kontrola signalizace podm�nky
                if(result != 0) {
                    // Tisk chyby
                    perror("readingThread pthread_cond_signal");
                    // TODO: exit?
                }
            } else {
                // Pokud posledn� na�ten� znak nen� konec ��dku,
                // vstup byl del�� ne� 512 znak�, co� nen� povoleno

                // Tisk chyby
                fprintf(stderr, "Chyba: P��li� dlouh� vstup.\n");

                // Cyklus pro na�ten� cel�ho zbytku vstupu
                while(inputLength > 0) {
                    // Na�ten� maxim�ln� 513 znak� ze vstupu do bufferu
                    inputLength = read(fileno(stdin), buffer, (MAX_INPUT_SIZE + 1));
                    // Kontrola na�ten� vstupu
                    if(inputLength == -1) {
                        // Tisk chyby
                        perror("readingThread: too long input read");
                        // Ukon�en� cyklu
                        break;
                    }
                }
            }
        }

        // Uvoln�n� mutexu
        result = pthread_mutex_unlock(&mutex);
        // Kontrola uvoln�n� mutexu
        if(result != 0) {
            // Tisk chyby
            perror("readingThread pthread_mutex_unlock");
            // TODO: exit?
        }
    }

    // Ukon�en� vl�kna bez chyby
    exit(EXIT_SUCCESS);
}

/*
 * Funkce s obsahem programu pro pracuj�c� vl�kno
 *
 * arg - argument pos�lan� z funkce pthread_create
 */
void *workingThread(void *arg) {
    // Prom�nn� pro stav KA zpracov�v�n� vstupu
    int KAState = START;
    // Prom�nn� pro n�vratov� hodnoty funkc�
    int result = 0;
    // Hlavn� smy�ka vl�kna, st�le zpracov�v� buffer
    while(state != EXIT) {
        // Nastaven� stavu KA na nov� vstup
        KAState = START;
        // Uzam�en� mutexu
        result = pthread_mutex_lock(&mutex);
        // Kontrola uzam�en� mutexu
        if(result != 0) {
            // Tisk chyby
            perror("workingThread pthread_mutex_lock");
            // TODO: exit?
        }
        // �ek�n�, dokud nen� povoleno zpracov�vat
        while(state != WORK) {
            // �ek�n� na zm�nu podm�nky
            result = pthread_cond_wait(&cond, &mutex);
            // Kontrola �ek�n� na podm�nku
            if(result != 0) {
                // Tisk chyby
                perror("workingThread pthread_cond_wait");
                // TODO: exit?
            }

            // Pokud je stav EXIT
            if(state == EXIT) {
                // Konec vl�kna
                exit(EXIT_SUCCESS);
            }
        }

        // Struktura pro nov� spou�t�n� program
        PROGRAM p;
        // Inicializace struktury pro program
        initStruct(&p);

        // Zpracov�n� vstupu po znac�ch kone�n�m automatem
        for(int i = 0; i < inputLength; i++) {
            // Roznodnut� pro v�tev KA
            switch(KAState) {
                // Za��tek vstupu nebo nov�ho argumentu
                case START: {
                    // P�esko�en� mezery nebo konce vstupu
                    if(buffer[i] == ' ' || buffer[i] == '\0') {
                        // Skok na dal�� iteraci
                        continue;
                    } else if(buffer[i] == '<') {
                        // Pokud je na vstupu znak p�esm�rov�n� vstupu,
                        // automat bude ve stavu na��t�n� vstupn�ho souboru
                        KAState = INPUT;
                    } else if(buffer[i] == '>') {
                        // Pokud je na vstupu znak p�esm�rov�n� v�stupu,
                        // automat bude ve stavu na��t�n� v�stupn�ho souboru
                        KAState = OUTPUT;
                    } else if(buffer[i] == '&') {
                        // Pokud je na vstupu znak b�hu na pozad�,
                        // nastav� se b�h na pozad� spou�t�n�ho programu
                        setBgRun(&p, 1);
                    } else {
                        // Pokud je na vstupu n�co jin�ho,
                        // jedn� se o argument spou�t�n�ho programu.

                        // Vytvo�� se nov� argument programu
                        newArg(&p, buffer[i]);
                        // Automat bude ve stavu na��t�n� argumentu
                        KAState = ARG;
                    }
                    // Konec v�tve vstupu nebo nov�ho argumentu
                    break;
                }
                // Na��t�n� vstupn�ho souboru
                case INPUT: {
                    // Pokud je na vstupu mezera nebo konec vstupu
                    if(buffer[i] == ' ' || buffer[i] == '\0') {
                        // Konec n�zvu vstupn�ho souboru
                        addInputChar(&p, '\0');
                        // Nastaven� stavu na nov� za��tek
                        KAState = START;
                    } else {
                        // Jinak se do n�zvu vstupn�ho souboru vlo�� dal�� znak
                        addInputChar(&p, buffer[i]);
                    }
                    // Konec v�tve na��t�n� vstupn�ho souboru
                    break;
                }
                // Na��t�n� v�stupn�ho souboru
                case OUTPUT: {
                    // Pokud je na vstupu mezera nebo konec vstupu
                    if(buffer[i] == ' ' || buffer[i] == '\0') {
                        // Konec n�zvu v�stupn�ho souboru
                        addOutputChar(&p, '\0');
                        // Nastaven� stavu na nov� za��tek
                        KAState = START;
                    } else {
                        // Jinak se do n�zvu v�stupn�ho souboru vlo�� dal�� znak
                        addOutputChar(&p, buffer[i]);
                    }
                    // Konec v�tve na��t�n� v�stupn�ho souboru
                    break;
                }
                // Na��t�n� argumentu programu
                case ARG: {
                    // Pokud je na vstupu mezera nebo konec vstupu
                    if(buffer[i] == ' ' || buffer[i] == '\0') {
                        // Konec argumentu programu
                        addArgChar(&p, '\0');
                        // Nastaven� stavu na nov� za��tek
                        KAState = START;
                    } else {
                        // Jinak se do argumentu vlo�� dal�� znak
                        addArgChar(&p, buffer[i]);
                    }
                    // Konec v�tve na��t�n� argumentu
                    break;
                }
            }
        }

        // Vytvo�en� podprocesu pro spu�t�n� zadan�ho programu
        pid_t pid = fork();

        // ROZV�TVEN� PROCES�
        // Pokud nastala chyba p�i vytvo�en� nov�ho procesu
        if(pid == -1) {
            // Tisk chyby
            perror("fork");
        } else if(pid == 0) {
            // POKUD SE JEDN� O NOV� PROCES PRO SPU�T�N�

            // Prom�nn� pro n�vratov� hodnoty funkc�
            int resultChild = 0;
            // Pokud m� proces b�et na pozad�
            if(p.bgRun == 1) {
                // Seznam sign�l� k blokov�n�
                sigset_t sigintSet;
                // Vypr�zdn�n� seznamu sign�l�
                resultChild = sigemptyset(&sigintSet);
                // Kontrola vypr�zdn�n� seznamu
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("sigemptyset");
                }
                // P�id�n� sign�lu SIGINT do seznamu
                resultChild = sigaddset(&sigintSet, SIGINT);
                // Kontrola p�id�n� sign�lu do seznamu
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("sigaddset");
                }
                // Blokov�n� seznamu sign�l�
                resultChild = sigprocmask(SIG_BLOCK, &sigintSet, NULL);
                // Kontrola nastaven� blokov�n� seznamu
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("sigprocmask");
                }
            }

            // Pokud byl zad�n vstupn� soubor
            if(p.inputFileLength > 0) {
                // Otev�en� vstupn�ho souboru pro �ten�
                int inputFile = open(p.inputFile, O_RDONLY);
                // Kontrola otev�en� souboru
                if(inputFile == -1) {
                    // Tisk chyby
                    perror("open inputFile");
                    // Ukon�en� procesu s chybou
                    exit(EXIT_FAILURE);
                }
                // Uzav�en� standardn�ho vstupu
                resultChild = close(0);
                // Kontrola uzav�en�
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("close input");
                    // Ukon�en� procesu s chybou
                    exit(EXIT_FAILURE);
                }
                // Duplikace otev�en�ho souboru pro vstup
                resultChild = dup(inputFile);
                // Kontrola duplikace
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("dup inputFile");
                    // Ukon�en� procesu s chybou
                    exit(EXIT_FAILURE);
                }
                // Uzav�en� otev�en�ho souboru
                resultChild = close(inputFile);
                // Kontrola uzav�en�
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("close inputFile");
                    // Ukon�en� procesu s chybou
                    exit(EXIT_FAILURE);
                }
            }

            // Pokud byl zad�n v�stupn� soubor
            if(p.outputFileLength > 0) {
                // Nastaven� p��znak� pro z�pis a vytvo�en� v�stupn�ho souboru
                int flags = O_WRONLY | O_CREAT;
                // Nastaven� m�du p��stupov�ch pr�v k souboru
                mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
                // Otev�en� v�stupn�ho souboru s p��znaky a pr�vy
                int outputFile = open(p.outputFile, flags, mode);
                // Kontrola otev�en� v�stupu
                if(outputFile == -1) {
                    // Tisk chyby
                    perror("open outputFile");
                    // Ukon�en� procesu s chybou
                    exit(EXIT_FAILURE);
                }
                // Uzav�en� standardn�ho v�stupu
                resultChild = close(1);
                // Kontrola uzav�en� v�stupu
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("close output");
                    // Ukon�en� procesu s chybou
                    exit(EXIT_FAILURE);
                }
                // Duplikace otev�en�ho souboru pro v�stup
                resultChild = dup(outputFile);
                // Kontrola duplikace
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("dup outputFile");
                    // Ukon�en� procesu s chybou
                    exit(EXIT_FAILURE);
                }
                // Uzav�en� otev�en�ho souboru
                close(outputFile);
                // Kontrola uzav�en�
                if(resultChild == -1) {
                    // Tisk chyby
                    perror("close outputFile");
                    // Ukon�en� procesu s chybou
                    exit(EXIT_FAILURE);
                }
            }
            // Spu�t�n� zadan�ho programu s jeho argumenty
            execvp(p.args[0], p.args);
            // SEM SE KOREKTN� SPU�T�N� NOV� PROCES NEMٮE DOSTAT

            // V�pis chyby, pokud se nepoda�ilo spustit program
            perror(p.args[0]);
            // Ukon�en� podprocesu s chybou
            exit(EXIT_FAILURE);
        } else {
            // POKUD SE JEDN� O RODI�OVSK� PROCES

            // Pokud nen� nastaven p��znak b�hu na pozad�
            if(p.bgRun == 0) {
                // Nastaven� p��znaku b��c�ho programu na pop�ed�
                isRunning = 1;
                // �ek�n� na ukon�en� spu�t�n�ho programu
                while(waitpid(pid, NULL, 0) != pid) {
                    // Pokud bylo �ek�n� p�eru�eno
                    if(errno == EINTR) {
                        // Pokra�uj v �ek�n�
                        continue;
                    } else if(errno == ECHILD) {
                        // Pokud nen� potomek, na kter�ho lze �ekat,
                        // ukon�i �ek�n�
                        break;
                    } else {
                        // Pokud nastala jin� chyba,
                        // tisk chyby
                        perror("parent waitpid");
                        // Konec �ek�n�
                        break;
                    }
                }
                // Nastaven� p��znaku neb��c�ho programu na pop�ed�
                isRunning = 0;
            }
        }

        // Uvoln�n� pam�ti po struktu�e programu
        freeStruct(&p);
        // Nastaven� bufferu na pr�zdn� �et�zec
        buffer[0] = '\0';
        // Zm�na stavu programu na �ten�
        state = READ;
        // Signalizace zm�ny podm�nky
        result = pthread_cond_signal(&cond);
        // Kontrola signalizace podm�nky
        if(result != 0) {
            // Tisk chyby
            perror("workingThread pthread_cond_signal");
            // TODO: exit?
        }
        // Uvoln�n� mutexu
        result = pthread_mutex_unlock(&mutex);
        // Kontrola uvoln�n� mutexu
        if(result != 0) {
            // Tisk chyby
            perror("workingThread pthread_mutex_unlock");
            // TODO: exit?
        }
    }

    // Ukon�en� vl�kna bez chyby
    exit(EXIT_SUCCESS);
}

/*
 * Hlavn� funkce main.
 *
 * argc - po�et argument� p��kazov�ho ��dku
 * argv - argumenty p��kazov� ��dky
 *
 * N�vratov� hodnota:
 *         0 - program prob�hl v po��dku
 *     jinak - program skon�il s chybou
 */
int main(int argc, char *argv[]) {
    // Prom�nn� pro ulo�en� n�vratov� hodnoty programu
    int returnValue = EXIT_SUCCESS;
    // Prom�nn� pro n�vratov� hodnoty funkc�
    int result = 0;
    // Prom�nn� pro �tec� vl�kno
    pthread_t reader;
    // Prom�nn� pro pracuj�c� vl�kno
    pthread_t worker;
    // Struktura pro nastaven� reakce na sign�l od potomk�
    struct sigaction childAct;
    // Struktura pro nastaven� reakce na sign�l p�eru�en�
    struct sigaction sigintAct;

    // Vynulov�n� struktury childAct
    memset(&childAct, 0, sizeof(childAct));
    // Vynulov�n� struktury sigintAct
    memset(&sigintAct, 0, sizeof(sigintAct));
    // Nastaven� funkce pro reakci na sign�l od potomk�
    childAct.sa_handler = childHandler;
    // Nastaven� funkce pro reakci na sign�l p�eru�en�
    sigintAct.sa_handler = sigintHandler;

    // P�i�azen� funkce k sign�lu od potomk�
    if(sigaction(SIGCHLD, &childAct, NULL)) {
        // Tisk chyby
        perror("sigaction childAct");
        // Ukon�en� programu s chybou
        exit(EXIT_FAILURE);
    }
    // P�i�azen� funkce k sign�lu p�eru�en�
    if(sigaction(SIGINT, &sigintAct, NULL)) {
        // Tisk chyby
        perror("sigaction sigintAct");
        // Ukon�en� programu s chybou
        exit(EXIT_FAILURE);
    }

    // Vytvo�en� �tec�ho vl�kna
    result = pthread_create(&reader, NULL, readingThread, NULL);
    // Kontrola vytvo�en� vl�kna
    if(result != 0) {
        // Tisk chyby
        perror("pthread_create reader");
        // Ukon�en� programu s chybou
        exit(EXIT_FAILURE);
    }
    // Vytvo�en� pracuj�c�ho vl�kna
    result = pthread_create(&worker, NULL, workingThread, NULL);
    // Kontrola vytvo�en� vl�kna
    if(result != 0) {
        // Tisk chyby
        perror("pthread_create worker");
        // Zru�en� �tec�ho vl�kna
        result = pthread_cancel(reader);
        // Kontrola zru�en� �tec�ho vl�kna
        if(result != 0) {
            // Tisk chyby
            perror("pthread_cancel reader");
        } else {
            // �ek�n� na ukon�en� zru�en�ho vl�kna
            result = pthread_join(reader, NULL);
            // Kontrola �ek�n� na ukon�en� zru�en�ho vl�kna
            if(result != 0) {
                // Tisk chyby
                perror("pthread_join reader");
            }
        }
        // Ukon�en� programu s chybou
        exit(EXIT_FAILURE);
    }

    // �ek�n� na ukon�en� �tec�ho vl�kna
    result = pthread_join(reader, NULL);
    // Kontrola ukon�en� �tec�ho vl�kna
    if(result != 0) {
        // Tisk chyby
        perror("pthread_join reader");
        // Nastaven� n�vratov� hodnoty na chybovou
        returnValue = EXIT_FAILURE;
    }
    // �ek�n� na ukon�en� pracuj�c�ho vl�kna
    result = pthread_join(worker, NULL);
    // Kontrola ukon�en� pracuj�c�ho vl�kna
    if(result != 0) {
        // Tisk chyby
        perror("pthread_join worker");
        // Ukon�en� programu s chybou
        exit(EXIT_FAILURE);
    }

    // Ukon�en� programu podle nastaven� n�vratov� hodnoty
    exit(returnValue);
}

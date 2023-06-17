#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>
#include <pwd.h>

// program wykonuje sie dopoki jest rowne true
bool czy_zakonczyc;

// potrzebne do loginu i katalogu domowego
int id_uzytkownika;
struct passwd* uzytkownik;
char* login;
char* katalog_domowy;

// ostatnio wpisane polecenia
char historia_polecen[11][4096];
// numery pojawienia sie polecen
int numery_polecen[11];
// indeks polecenia obecnie wpisywanego
// ( polecenie moze miec kilka linii )
int obecne_polecenie;
// numer pojawienia sie 'obecne_polecenie'
int numer_obecnego_polecenia;

// sciezka poprzedniego katalogu
char poprzedni_katalog[1024];
// current working directory
char cwd[1024];

// w zaleznosci od 'stan' inna interpretacja
//  analizowanego znaku z polecenia
enum { PRZERWA, ARGUMENT, CUDZYSLOW, KOMENTARZ } stan;
// czy polecenie ma byc kontynuowane w nastepnej linii
bool czy_dokonczyc_polecenie;
// czy ostatni analizowany znak to backslash
bool czy_backslash;

// znak z polecenia do interpretacji
char znak;
// wpisane polecenie ( jedna linia polecenia )
char polecenie[4096];
// nowy argument tworzony na podstawie znakow
char argument[4096];
// nazwa komendy z argumentami do exec*()
char* argumenty[11];
// liczba argumenow polecenia ( z nazwa komendy )
int liczba_argumentow;
// jesli polecenie nie zawiera niczego to jest ignorowane
bool czy_puste_polecenie;

void help ()
{
    printf ( "\nProjekt microshell : Marcin Marlewski \n\n" );
    printf ( "Polecenie exit : wyscie z programu \n\n" );
    printf ( "Polecenie cd : przejscie do innego katalogu \n" );
    printf ( ".. : przejscie do katalogu wyzej \n" );
    printf ( "-  : przejscie do poprzedniego katalogu \n" );
    printf ( "~  : przejscie do katalogu domowego \n" );
    printf ( "brak argumentow : to samo co wyzej \n\n" );
    printf ( "Polecenie history : wypisanie ostatnich 10 polecen \n\n" );
    printf ( "Polecenie tree : wypisanie zawartosci podanego katalogu oraz jego podkatalogow w postaci drzewa \n\n" );
    printf ( "Wpisanie '#' rozpoczyna komentarz, wszystko wpisane jako komentarz jest ignorowane\n\n" );
    printf ( "Wpisanie '\\' na koncu polecenia pozwoli na kontynuowanie go w nastepnej lini \n\n" );
    printf ( "Dzieki ';' mozna umiescic kilka polecen w jednej linii \n\n" );
    printf ( "Wszystko miedzy znakami '\"' jest trktowane jako jeden argument, w tym odstepy\n\n" );
    printf ( "Niedomkniecie '\"' wymusi dokonczenie polecenia w nastepnej linii \n\n" );
    printf ( "Umieszczenie '\\' przed znakiem specjalnym spowoduje zapisanie go jak normalny znak \n\n" );
    printf ( "Miedzy '\"' znaki specjalne sa traktowane jak zwykle ( z wyjatkiem '\"' )\n\n" );
}

// wypisanie zawartosci katalogu i jego podkatalogow
void tree ( char* sciezka, int glebokosc, char* przerwa )
{
    DIR* katalog = opendir ( sciezka );

    // zapisywanie odpowiedniej zawartosci katalogu

    if ( katalog != 0 )
    {
        struct dirent* zawartosc_katalogu[100];
        struct dirent* zawartosc;
        int liczba_zawartosci = 0;

        for ( int i = 0; i < 100; i++ )
        {
            zawartosc = readdir ( katalog );

            if ( zawartosc != 0 )
            {
                if ( ( zawartosc->d_type == DT_REG || zawartosc->d_type == DT_DIR ) &&
                     !( zawartosc->d_type == DT_DIR && ( strcmp ( zawartosc->d_name, "." ) == 0 || strcmp ( zawartosc->d_name, ".." ) == 0 ) ) )
                {
                    zawartosc_katalogu[i] = zawartosc;
                    liczba_zawartosci++;
                }
                else i--;
            }
            else break;
        }

        // wypisywanie odpowiedniej zawartosci katalogu

        for ( int i = 0; i < liczba_zawartosci; i++ )
        {
            fprintf ( stdout, "%s", przerwa );

            if ( i == liczba_zawartosci - 1 )
            {
                fprintf ( stdout, "\u2514\u2500" );
            }
            else
            {
                fprintf ( stdout, "\u251c\u2500" );
            }

            if ( zawartosc_katalogu[i]->d_type == DT_DIR )
            {
                fprintf ( stdout, "\e[1;34m%s\n\e[0m", zawartosc_katalogu[i]->d_name );
            }
            else
            {
                fprintf ( stdout, "%s\n", zawartosc_katalogu[i]->d_name );
            }

            // podkatalog

            if ( zawartosc_katalogu[i]->d_type == DT_DIR )
            {
                char nowa_sciezka[1024];
                sprintf ( nowa_sciezka, "%s/%s", sciezka, zawartosc_katalogu[i]->d_name );
                char nowa_przerwa[1024];

                if ( i == liczba_zawartosci - 1 )
                {
                    sprintf ( nowa_przerwa, "%s  ", przerwa );
                }
                else
                {
                    sprintf ( nowa_przerwa, "%s\u2502 ", przerwa );
                }

                tree ( nowa_sciezka, glebokosc + 1, nowa_przerwa );
            }
        }

        closedir ( katalog );
    }
    else
    {
        perror("Nie mozna wypisac zawartosci katalogu ");
    }
}

// dodaje nowo stworzony argument do tablicy
void dodaj_argument ()
{
    if ( liczba_argumentow < 11 )
    {
        argumenty[liczba_argumentow] = strdup ( argument );

        liczba_argumentow++;
    }

    strcpy ( argument, "" );
}

// usuwa argumenty na nowe polecenie
void zwolnij_argumenty ()
{
    for ( int i = 0; i < liczba_argumentow; i++ )
    {
        free ( argumenty[i] );
        argumenty[i] = 0;
    }

    liczba_argumentow = 0;
}

void wykonaj_polecenie ()
{
    if ( liczba_argumentow == 0 )
    {
        return;
    }
    else if ( strcmp ( argumenty[0], "exit" ) == 0 )
    {
        if ( liczba_argumentow == 1 )
        {
            czy_zakonczyc = true;
        }
        else
        {
            fprintf ( stderr, "Nieprawidlowa liczba argumentow w poleceniu exit\n" );
        }
    }
    else if ( strcmp ( argumenty[0], "cd" ) == 0 )
    {
        // odpowiedni katalog w miejsce symbolu

        if ( liczba_argumentow == 1 )
        {
            argumenty[1] = strdup ( katalog_domowy );
            liczba_argumentow++;
        }
        else if ( liczba_argumentow == 2 )
        {
            if ( strcmp ( argumenty[1], "-" ) == 0 )
            {
                strcpy ( argumenty[1], poprzedni_katalog );
            }
            else if ( strcmp ( argumenty[1], "~" ) == 0 )
            {
                strcpy ( argumenty[1], katalog_domowy );
            }
        }
        else
        {
            fprintf ( stderr, "Nieprawidlowa liczba argumentow w poleceniu cd\n" );
            return;
        }

        // zmiana katalogu

        char obecny_katalog[1024];
        strcpy ( obecny_katalog, cwd );

        if ( chdir ( argumenty[1] ) == 0 )
        {
            strcpy ( poprzedni_katalog, obecny_katalog );

            if ( getcwd ( cwd, 1024 ) == 0 )
            {
                strcpy ( cwd, "-error-" );
            }
        }
        else
        {
            perror ( "Nie mozna przejsc do katalogu" );
        }
    }
    else if ( strcmp ( argumenty[0], "history" ) == 0 )
    {
        if ( liczba_argumentow == 1 )
        {
            for ( int i = 0; i < obecne_polecenie; i++ )
            {
                fprintf ( stdout, "%i %s", numery_polecen[i], historia_polecen[i] );
            }
        }
        else
        {
            fprintf ( stderr, "Nieprawidlowa liczba argumentow w poleceniu history\n" );
        }
    }
    else if ( strcmp ( argumenty[0], "tree" ) == 0 )
    {
        if ( liczba_argumentow == 1 )
        {
            fprintf ( stdout, ".\n" );
            tree ( ".", 0, "" );
        }
        else if ( liczba_argumentow == 2 )
        {
            fprintf ( stdout, "%s\n", argumenty[1] );
            tree ( argumenty[1], 0, "" );
        }
        else
        {
            fprintf ( stderr, "Nieprawidlowa liczba argumentow w poleceniu tree\n" );
        }
    }
    else if ( strcmp ( argumenty[0], "help" ) == 0 )
    {
        if ( liczba_argumentow == 1 )
        {
            help ();
        }
        else
        {
            fprintf ( stderr, "Nieprawidlowa liczba argumentow w poleceniu help\n" );
        }
    }
    else
    {
        // fork() i exec*()

        int pid = fork ();

        if ( pid == -1 )
        {
            fprintf ( stderr, "Nie mozna stworzyc podprocesu\n" );
        }
        else if ( pid == 0 )
        {
            execvp ( argumenty[0], argumenty );
            perror ( "Nie mozna wykonac programu" );
            exit ( 0 );
        }
        else
        {
            waitpid ( pid, 0, 0 );
        }
    }
}

int main ()
{
    // uzytkownik

    id_uzytkownika = getuid ();
    uzytkownik = getpwuid ( id_uzytkownika );

    if ( uzytkownik == 0 )
    {
        login = "error";
        katalog_domowy = ".";
    }
    else
    {
        login = uzytkownik->pw_name;
        katalog_domowy = uzytkownik->pw_dir;
    }

    // historia

    for ( int i = 0; i < 11; i++ )
    {
        numery_polecen[i] = 0;
        strcpy ( historia_polecen[i], "" );
    }

    // cwd

    if ( getcwd ( cwd, 4096 ) == 0 )
    {
        strcpy ( cwd, "error" );
    }

    // reszta

    czy_zakonczyc = false;
    obecne_polecenie = 0;
    numer_obecnego_polecenia = 1;
    strcpy ( poprzedni_katalog, "." );
    stan = PRZERWA;
    czy_dokonczyc_polecenie = false;
    czy_backslash = false;

    // petla

    while ( czy_zakonczyc == false )
    {
        // kontynuowanie polecenia
        if ( czy_dokonczyc_polecenie )
        {
            fprintf ( stdout, "> " );
            strcpy ( polecenie, "" );
            fgets ( polecenie, 4096, stdin );
        }
        // nowe polecenie
        else
        {
            zwolnij_argumenty ();
            strcpy ( polecenie, "" );
            strcpy ( argument, "" );
            fprintf ( stdout, "[\e[1;36m%s\e[0m:\e[1;32m%s\e[0m] $ ", login, cwd );
            fgets ( polecenie, 4096, stdin );
        }

        // niedokonczonego polecenia nie mozna zignorowac
        if ( !czy_dokonczyc_polecenie )
        {
            czy_puste_polecenie = true;

            for ( int i = 0; i < strlen ( polecenie ) - 1; i++ )
            {
                if ( polecenie[i] != ' ' && polecenie[i] != '\t' )
                {
                    czy_puste_polecenie = false;
                    break;
                }
            }

            if ( czy_puste_polecenie ) continue;
        }
        else
        {
            czy_dokonczyc_polecenie = false;
        }

        // ponowne wykonanie ostatniego polecenia
        if ( strcmp ( polecenie, "!!\n" ) == 0 && obecne_polecenie > 0 )
        {
            strcpy ( polecenie, historia_polecen[obecne_polecenie - 1] );
        }

        // analizowanie polecenia znak po znaku
        for ( int i = 0; i < strlen ( polecenie ); i++ )
        {
            znak = polecenie[i];

            switch ( stan )
            {
                case PRZERWA:

                switch ( znak )
                {
                    case '\\':

                    if ( czy_backslash )
                    {
                        sprintf ( argument, "%s%c", argument, znak );
                        stan = ARGUMENT;
                        czy_backslash = false;
                    }
                    else czy_backslash = true;

                    break;
                    case '"':

                    if ( czy_backslash )
                    {
                        sprintf ( argument, "%s%c", argument, znak );
                        stan = ARGUMENT;
                        czy_backslash = false;
                    }
                    else stan = CUDZYSLOW;

                    break;
                    case '#':

                    if ( czy_backslash )
                    {
                        sprintf ( argument, "%s%c", argument, znak );
                        stan = ARGUMENT;
                        czy_backslash = false;
                    }
                    else stan = KOMENTARZ;

                    break;
                    case ';':

                    if ( czy_backslash )
                    {
                        sprintf ( argument, "%s%c", argument, znak );
                        stan = ARGUMENT;
                        czy_backslash = false;
                    }
                    else
                    {
                        wykonaj_polecenie ();
                        zwolnij_argumenty ();
                    }

                    break;
                    case ' ':

                    if ( czy_backslash )
                    {
                        sprintf ( argument, "%s%c", argument, znak );
                        stan = ARGUMENT;
                        czy_backslash = false;
                    }

                    break;
                    case '\n':

                    if ( czy_backslash )
                    {
                        czy_dokonczyc_polecenie = true;
                    }
                    else wykonaj_polecenie ();

                    break;
                    default:

                    if ( czy_backslash ) czy_backslash = false;

                    sprintf ( argument, "%s%c", argument, znak );
                    stan = ARGUMENT;

                    break;
                }

                break;
                case ARGUMENT:

                switch ( znak )
                {
                    case '\\':

                    if ( czy_backslash )
                    {
                        sprintf ( argument, "%s%c", argument, znak );
                        czy_backslash = false;
                    }
                    else czy_backslash = true;

                    break;
                    case ' ':

                    if ( czy_backslash )
                    {
                        sprintf ( argument, "%s%c", argument, znak );
                        czy_backslash = false;
                    }
                    else
                    {
                        dodaj_argument ();
                        stan = PRZERWA;
                    }

                    break;
                    case ';':

                    if ( czy_backslash )
                    {
                        sprintf ( argument, "%s%c", argument, znak );
                        czy_backslash = false;
                    }
                    else
                    {
                        dodaj_argument ();
                        wykonaj_polecenie ();
                        zwolnij_argumenty ();
                    }

                    break;
                    case '\n':

                    if ( czy_backslash )
                    {
                        czy_dokonczyc_polecenie = true;
                    }
                    else
                    {
                        dodaj_argument ();
                        wykonaj_polecenie ();
                        stan = PRZERWA;
                    }

                    break;
                    default:

                    if ( czy_backslash ) czy_backslash = false;

                    sprintf ( argument, "%s%c", argument, znak );

                    break;
                }

                break;
                case CUDZYSLOW:

                switch ( znak )
                {
                    case '\\':

                    if ( czy_backslash )
                    {
                        sprintf ( argument, "%s%c", argument, znak );
                        czy_backslash = false;
                    }
                    else czy_backslash = true;

                    break;
                    case '"':

                    if ( czy_backslash )
                    {
                        sprintf ( argument, "%s%c", argument, znak );
                        czy_backslash = false;
                    }
                    else
                    {
                        dodaj_argument ();
                        stan = PRZERWA;
                    }

                    break;
                    case '\n':

                    czy_dokonczyc_polecenie = true;

                    break;
                    default:

                    if ( czy_backslash )
                    {
                        sprintf ( argument, "%s\\", argument );
                        czy_backslash = false;
                    }

                    sprintf ( argument, "%s%c", argument, znak );

                    break;
                }

                break;
                case KOMENTARZ:

                if ( znak == '\n' )
                {
                    wykonaj_polecenie ();
                    stan = PRZERWA;
                }

                break;
                default:
                break;
            }
        }

        // z niedokonczonego polecenia usuwany znak nowej linii i/lub backslash
        if ( czy_dokonczyc_polecenie )
        {
            if ( czy_backslash )
            {
                polecenie[strlen ( polecenie ) - 2] = '\0';
                czy_backslash = false;
            }
            else
            {
                polecenie[strlen ( polecenie ) - 1] = '\0';
            }
        }

        // zapisywanie polecenia do historii
        sprintf ( historia_polecen[obecne_polecenie], "%s%s", historia_polecen[obecne_polecenie], polecenie );

        // gdy polecenie jest zakonczone
        if ( !czy_dokonczyc_polecenie )
        {
            numery_polecen[obecne_polecenie] = numer_obecnego_polecenia;
            numer_obecnego_polecenia++;

            if ( obecne_polecenie == 10 )
            {
                for ( int i = 0; i < 10; i++ )
                {
                    numery_polecen[i] = numery_polecen[i + 1];
                    strcpy ( historia_polecen[i], historia_polecen[i + 1] );
                }

                numery_polecen[10] = 0;
                strcpy ( historia_polecen[10], "" );
            }
            else obecne_polecenie++;
        }
    }

    return 0;
}
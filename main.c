#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <math.h>

#define PIPE_READ 0
#define PIPE_WRITE 1

typedef int pipe_t;

// Struktura do przechowywania wyników obliczeń
typedef struct {
    double sinSum;     // Suma wartości funkcji sinus z próbek
    size_t samples;    // Liczba próbek
    char padding[64];  // Padding zapobiegający konfliktom cache między wątkami
} calcInfo;

// Globalne zmienne konfiguracyjne
size_t thread_num;        // Liczba wątków na proces
size_t subproc_num;       // Liczba procesów
size_t totalSamples;      // Łączna liczba punktów do wygenerowania
size_t samplesPerThread;  // Liczba punktów na każdy wątek

// Inicjalizuje strukturę wyników
void init_calc_info(calcInfo *info) {
    info->sinSum = 0.0;
    info->samples = 0;
}

// Zwraca losową wartość z przedziału [0, π]
double get_random_input(unsigned int *seed) {
    return ((double) rand_r(seed) / RAND_MAX) * M_PI;
}

// Funkcja wykonywana przez każdy wątek
void *compute_sin_sum(void *calcInfo_ptr) {
    // Rzutowanie wskaźnika na właściwy typ
    calcInfo *info = (calcInfo *) calcInfo_ptr;
    size_t localSamples = samplesPerThread;

    // Inicjalizacja lokalnego ziarna generatora losowego
    unsigned int seed = getpid() + pthread_self() + time(NULL);

    // Generowanie punktów i sumowanie wartości sin(x)
    for (size_t i = 0; i < localSamples; i++) {
        double x = get_random_input(&seed);  // Losowy x ∈ [0, π]
        info->sinSum += sin(x);              // Dodaj wartość sin(x) do sumy
        info->samples++;                     // Zwiększ licznik próbek
    }

    return NULL;
}

// Funkcja wywoływana w procesie potomnym - tworzy wątki i zbiera ich wyniki
void execute_fork(calcInfo *forkInfo) {
    pthread_t threads[thread_num];        // Tablica identyfikatorów wątków
    calcInfo threadInfos[thread_num];     // Tablica lokalnych struktur dla każdego wątku

    // Tworzenie wątków
    for (int i = 0; i < thread_num; i++) {
        init_calc_info(&threadInfos[i]);  // Zainicjalizuj dane dla wątku
        pthread_create(&threads[i], NULL, compute_sin_sum, &threadInfos[i]);  // Uruchom wątek
    }

    // Zbieranie wyników z wątków
    for (int i = 0; i < thread_num; i++) {
        pthread_join(threads[i], NULL);  // Poczekaj aż wątek się zakończy

        // Dodaj wyniki z wątku do sumy procesowej
        forkInfo->sinSum += threadInfos[i].sinSum;
        forkInfo->samples += threadInfos[i].samples;
    }
}

// Parsuje argumenty programu i ustawia zmienne globalne
void read_args(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Użycie: %s <liczba_punktów> <liczba_procesów> <liczba_wątków>\n", argv[0]);
        exit(1);
    }

    // Konwersja argumentów wejściowych na liczby
    totalSamples = atoi(argv[1]);
    subproc_num = atoi(argv[2]);
    thread_num = atoi(argv[3]);

    // Oblicz liczbę punktów przypadającą na jeden wątek
    samplesPerThread = totalSamples / (subproc_num * thread_num);
}

// Zwraca czas w milisekundach od epoki
long get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// Wyświetla parametry uruchomienia programu
void printProcessStatus() {
    printf("\n***** PARAMETRY URUCHOMIENIA *****\n");
    printf(" - totalSamples: %ld\n", totalSamples);
    printf(" - subproc_num:  %ld\n", subproc_num);
    printf(" - thread_num:   %ld\n\n", thread_num);
}

// Główna funkcja programu
int main(int argc, char **argv) {
    read_args(argc, argv);        // Odczytaj parametry wejściowe
    printProcessStatus();         // Wyświetl je użytkownikowi

    long timeStart = get_time_ms();  // Zmierz czas startu

    pid_t pid;
    pid_t child_pipes[subproc_num];     // Tablica PID-ów dzieci
    pipe_t pipes[subproc_num][2];       // Tablica pipe’ów do komunikacji
    calcInfo globalInfo;                // Struktura do sumowania wszystkich wyników
    init_calc_info(&globalInfo);        // Inicjalizacja struktury

    // Tworzenie procesów potomnych
    for (int i = 0; i < subproc_num; i++) {
        pipe(pipes[i]);              // Tworzenie pipe'a dla komunikacji z dzieckiem
        pid = fork();                // Tworzenie procesu potomnego
        child_pipes[i] = pid;

        if (pid == -1) {
            perror("Nie udało się utworzyć procesu");
            exit(1);
        } else if (pid == 0) {
            // Proces potomny wykonuje swoje obliczenia
            calcInfo localInfo;
            init_calc_info(&localInfo);

            execute_fork(&localInfo);  // Uruchom wątki i zbierz ich wyniki

            // Wyślij dane do procesu nadrzędnego przez pipe
            close(pipes[i][PIPE_READ]);
            write(pipes[i][PIPE_WRITE], &localInfo, sizeof(calcInfo));
            close(pipes[i][PIPE_WRITE]);

            return 0;  // Zakończ proces potomny
        }
    }

    // Proces główny zbiera dane od potomków
    for (int i = 0; i < subproc_num; i++) {
        calcInfo temp;

        waitpid(child_pipes[i], NULL, 0);  // Czekaj na zakończenie dziecka

        close(pipes[i][PIPE_WRITE]);      // Zamknij nieużywaną końcówkę pipe’a
        read(pipes[i][PIPE_READ], &temp, sizeof(calcInfo));  // Odczytaj dane
        close(pipes[i][PIPE_READ]);

        // Dodaj dane do wyniku globalnego
        globalInfo.sinSum += temp.sinSum;
        globalInfo.samples += temp.samples;
    }

    // Obliczenie średniej wartości sin(x)
    double average = globalInfo.sinSum / globalInfo.samples;

    long timeEnd = get_time_ms();  // Zmierz czas końcowy

    // Wyświetlenie wyników
    printf("Czas wykonania: %ld ms\n", timeEnd - timeStart);
    printf("Średnia wartość sin(x) dla losowych x ∈ [0, π]: %lf\n", average);

    return 0;
}

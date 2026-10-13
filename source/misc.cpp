#include "defs.h"
#include <cstdio>
#include <cstring>
#include <chrono>

#ifdef _WIN32
  #include "Windows.h"
  #include <io.h>
  #include <fcntl.h>
  #include <intrin.h>
  #include <conio.h>
#else
  #include <unistd.h>
  #include <sys/select.h>
  #include <sys/time.h>
  #include <chrono>
#endif

// get time in milliseconds
// 12/10/2026: su Windows era GetTickCount(), che avanza a scatti di ~15,6 ms: il controllo del tempo sforava di ~10 ms
// a mossa (go movetime 100 -> 109 ms) e ogni decisione sull'orologio aveva quella grana. Ora steady_clock (QPC) su
// tutte le piattaforme, come Stockfish, contato dall'avvio del processo (niente giro dell'int dopo 24,8 giorni).
int get_time_ms()
{
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return (int)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

int input_waiting()
{
#ifdef _WIN32
    return _kbhit();
#else
    // POSIX: stdin ha dati pronti? (poll non bloccante via select)
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    struct timeval tv = {0, 0};
    return select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0;
#endif
}

// read GUI/user input
void read_input()
{
    if (input_waiting())
    {
        char input[256] = "";
        int  bytes;
#ifdef _WIN32
        bytes = _read(_fileno(stdin), input, 255);
#else
        bytes = read(fileno(stdin), input, 255);
#endif
        // EOF (pipe chiusa) o errore: NON fermare la ricerca. Su Linux select()
        // segnala "leggibile" anche all'EOF: trattarlo come stop abortiva la
        // ricerca a depth fissa subito -> nessuna mossa -> path di rescue.
        if (bytes <= 0) return;
        input[bytes] = '\0';
        char* endc = strchr(input, '\n');
        if (endc) *endc = '\0';
        // Solo i comandi UCI di interruzione fermano la ricerca.
        if (!strncmp(input, "quit", 4)) { quit = 1; stopped = 1; }
        else if (!strncmp(input, "stop", 4)) { stopped = 1; }
    }
}

// a bridge function to interact between search and GUI input
void communicate() {
    if (timeset == 1 && get_time_ms() > stoptime) {
        stopped = 1;
    }
    read_input();
}

// count_bits / get_ls1b_index: ora `static inline` in defs.h (09/09/2026).


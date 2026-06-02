#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_W 640
#define MAX_H 480
#define MAX_PIX (MAX_W * MAX_H)

static int listen_port = 7000;
static char out_ip[64] = "192.168.60.1";
static int out_port = 6000;
static int threshold_dark = 80;
static int min_area = 250;

static const int ARUCO_IDS[5] = {0, 1, 2, 3, 4};

static const unsigned char ARUCO_PATTERNS[5][16] = {
    {0,1,0,0, 1,0,1,0, 1,1,0,0, 1,1,0,1}, /* id=0 */
    {1,1,1,1, 0,0,0,0, 0,1,1,0, 0,1,0,1}, /* id=1 */
    {1,1,0,0, 1,1,0,0, 1,1,0,1, 0,0,1,0}, /* id=2 */
    {0,1,1,0, 0,1,1,0, 1,0,1,1, 1,0,0,1}, /* id=3 */
    {1,0,1,0, 1,0,1,1, 0,1,1,0, 0,0,0,1}  /* id=4 */
};

static int udp_sock = -1;
static struct sockaddr_in out_addr;

static int read_n(int fd, void *buf, int n) {
    int got = 0;
    unsigned char *p = (unsigned char *)buf;
    while (got < n) {
        int r = recv(fd, p + got, n - got, 0);
        if (r <= 0) return -1;
        got += r;
    }
    return got;
}

static void rotate_4x4(const unsigned char in[16], unsigned char out[16], int rot) {
    int r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            int src = r * 4 + c;
            int rr = r, cc = c;
            if (rot == 1) { rr = c; cc = 3 - r; }
            else if (rot == 2) { rr = 3 - r; cc = 3 - c; }
            else if (rot == 3) { rr = 3 - c; cc = r; }
            out[rr * 4 + cc] = in[src];
        }
    }
}

static int hamming16(const unsigned char a[16], const unsigned char b[16]) {
    int i, d = 0;
    for (i = 0; i < 16; i++) {
        if (a[i] != b[i]) d++;
    }
    return d;
}

static int sample_dark_cell(uint8_t *g, int w, int h, int x0, int y0, int x1, int y1) {
    int sx0 = x0 + (x1 - x0) / 4;
    int sx1 = x1 - (x1 - x0) / 4;
    int sy0 = y0 + (y1 - y0) / 4;
    int sy1 = y1 - (y1 - y0) / 4;

    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 >= w) sx1 = w - 1;
    if (sy1 >= h) sy1 = h - 1;

    long sum = 0;
    int cnt = 0;
    int x, y;
    for (y = sy0; y <= sy1; y++) {
        for (x = sx0; x <= sx1; x++) {
            sum += g[y * w + x];
            cnt++;
        }
    }
    if (cnt <= 0) return 0;
    return (sum / cnt) < threshold_dark ? 1 : 0;
}

static int decode_aruco(uint8_t *g, int w, int h, int x0, int y0, int x1, int y1, int *best_score) {
    int bw = x1 - x0 + 1;
    int bh = y1 - y0 + 1;
    if (bw < 24 || bh < 24) return -1;

    float ratio = (float)bw / (float)bh;
    if (ratio < 0.60f || ratio > 1.65f) return -1;

    unsigned char grid[6][6];
    int gx, gy;
    for (gy = 0; gy < 6; gy++) {
        for (gx = 0; gx < 6; gx++) {
            int cx0 = x0 + (bw * gx) / 6;
            int cx1 = x0 + (bw * (gx + 1)) / 6 - 1;
            int cy0 = y0 + (bh * gy) / 6;
            int cy1 = y0 + (bh * (gy + 1)) / 6 - 1;
            grid[gy][gx] = sample_dark_cell(g, w, h, cx0, cy0, cx1, cy1);
        }
    }

    int border_dark = 0;
    for (gx = 0; gx < 6; gx++) {
        border_dark += grid[0][gx];
        border_dark += grid[5][gx];
    }
    for (gy = 1; gy < 5; gy++) {
        border_dark += grid[gy][0];
        border_dark += grid[gy][5];
    }

    if (border_dark < 14) return -1;

    unsigned char inner[16];
    int k = 0;
    for (gy = 1; gy <= 4; gy++) {
        for (gx = 1; gx <= 4; gx++) {
            inner[k++] = grid[gy][gx];
        }
    }

    int best_id = -1;
    int best = 999;
    int p, r;
    for (p = 0; p < 5; p++) {
        for (r = 0; r < 4; r++) {
            unsigned char rot[16];
            rotate_4x4(ARUCO_PATTERNS[p], rot, r);
            int d = hamming16(inner, rot);
            if (d < best) {
                best = d;
                best_id = ARUCO_IDS[p];
            }
        }
    }

    if (best_score) *best_score = best;
    if (best <= 4) return best_id;
    return -1;
}

static void send_perception(int seq, int aruco, int id, int cx, int area,
                            int obstacle, int obs_left, int obs_center, int obs_right) {
    (void)obstacle;
    (void)obs_left;
    (void)obs_center;
    (void)obs_right;

    /*
     * Final project rule:
     * - AI-G only reports ArUco marker IDs 0, 1, 2.
     * - ID 3, ID 4, and false detections are invalid.
     * - Obstacle/person/worker detection is not reported by AI-G.
     */
    if (aruco != 1 || id < 0 || id > 2) {
        aruco = 0;
        id = -1;
        cx = 320;
        area = 0;
    }

    char msg[256];
    snprintf(msg, sizeof(msg),
        "PERCEPTION seq=%d aruco=%d id=%d cx=%d area=%d",
        seq, aruco, id, cx, area);

    sendto(udp_sock, msg, strlen(msg), 0, (struct sockaddr *)&out_addr, sizeof(out_addr));
}

static void process_frame(uint8_t *g, int w, int h, int seq) {
    uint8_t *visited = (uint8_t *)calloc(w * h, 1);
    int *queue = (int *)malloc(sizeof(int) * w * h);

    if (!visited || !queue) {
        send_perception(seq, 0, -1, w / 2, 0, 0, 0, 0, 0);
        free(visited);
        free(queue);
        return;
    }

    int best_id = -1;
    int best_score = 999;
    int best_area = 0;
    int best_cx = w / 2;

    int obstacle = 0;
    int obs_left = 0, obs_center = 0, obs_right = 0;

    int x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int idx = y * w + x;
            if (visited[idx] || g[idx] >= threshold_dark) continue;

            int head = 0, tail = 0;
            queue[tail++] = idx;
            visited[idx] = 1;

            int minx = x, maxx = x, miny = y, maxy = y;
            int count = 0;

            while (head < tail) {
                int cur = queue[head++];
                int cy = cur / w;
                int cx = cur % w;
                count++;

                if (cx < minx) minx = cx;
                if (cx > maxx) maxx = cx;
                if (cy < miny) miny = cy;
                if (cy > maxy) maxy = cy;

                int nx, ny, ni;

                nx = cx + 1; ny = cy;
                if (nx < w) {
                    ni = ny * w + nx;
                    if (!visited[ni] && g[ni] < threshold_dark) {
                        visited[ni] = 1; queue[tail++] = ni;
                    }
                }

                nx = cx - 1; ny = cy;
                if (nx >= 0) {
                    ni = ny * w + nx;
                    if (!visited[ni] && g[ni] < threshold_dark) {
                        visited[ni] = 1; queue[tail++] = ni;
                    }
                }

                nx = cx; ny = cy + 1;
                if (ny < h) {
                    ni = ny * w + nx;
                    if (!visited[ni] && g[ni] < threshold_dark) {
                        visited[ni] = 1; queue[tail++] = ni;
                    }
                }

                nx = cx; ny = cy - 1;
                if (ny >= 0) {
                    ni = ny * w + nx;
                    if (!visited[ni] && g[ni] < threshold_dark) {
                        visited[ni] = 1; queue[tail++] = ni;
                    }
                }
            }

            if (count < min_area) continue;

            int bw = maxx - minx + 1;
            int bh = maxy - miny + 1;
            int rect_area = bw * bh;

            int score = 999;
            int id = decode_aruco(g, w, h, minx, miny, maxx, maxy, &score);

            if (id >= 0 && id <= 2) {
                if (score < best_score || (score == best_score && rect_area > best_area)) {
                    best_score = score;
                    best_id = id;
                    best_area = rect_area;
                    best_cx = (minx + maxx) / 2;
                }
            } else {
                int ccx = (minx + maxx) / 2;
                if (rect_area > 3000 && maxy > (h * 55) / 100) {
                    obstacle = 1;
                    if (ccx < w / 3) obs_left = 1;
                    else if (ccx > (w * 2) / 3) obs_right = 1;
                    else obs_center = 1;
                }
            }
        }
    }

    if (best_id >= 0) {
        send_perception(seq, 1, best_id, best_cx, best_area, obstacle, obs_left, obs_center, obs_right);
    } else {
        send_perception(seq, 0, -1, w / 2, 0, obstacle, obs_left, obs_center, obs_right);
    }

    free(visited);
    free(queue);
}

static int make_server(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    struct sockaddr_in addr;

    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(s, 1) < 0) {
        perror("listen");
        exit(1);
    }

    return s;
}

static void parse_args(int argc, char **argv) {
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--listen-port") && i + 1 < argc) {
            listen_port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--out-ip") && i + 1 < argc) {
            strncpy(out_ip, argv[++i], sizeof(out_ip) - 1);
        } else if (!strcmp(argv[i], "--out-port") && i + 1 < argc) {
            out_port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--threshold") && i + 1 < argc) {
            threshold_dark = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--min-area") && i + 1 < argc) {
            min_area = atoi(argv[++i]);
        }
    }
}

int main(int argc, char **argv) {
    parse_args(argc, argv);

    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&out_addr, 0, sizeof(out_addr));
    out_addr.sin_family = AF_INET;
    out_addr.sin_port = htons(out_port);
    inet_aton(out_ip, &out_addr.sin_addr);

    int server = make_server(listen_port);

    printf("[AI-G NET] receiver start tcp_port=%d out=%s:%d threshold=%d min_area=%d\n",
           listen_port, out_ip, out_port, threshold_dark, min_area);
    fflush(stdout);

    while (1) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int c = accept(server, (struct sockaddr *)&cli, &clen);
        if (c < 0) continue;

        printf("[AI-G NET] client connected\n");
        fflush(stdout);

        while (1) {
            char magic[4];
            uint32_t nseq, nlen;
            uint16_t nw, nh;

            if (read_n(c, magic, 4) < 0) break;
            if (memcmp(magic, "FRAM", 4) != 0) break;

            if (read_n(c, &nseq, 4) < 0) break;
            if (read_n(c, &nw, 2) < 0) break;
            if (read_n(c, &nh, 2) < 0) break;
            if (read_n(c, &nlen, 4) < 0) break;

            int seq = (int)ntohl(nseq);
            int w = (int)ntohs(nw);
            int h = (int)ntohs(nh);
            int len = (int)ntohl(nlen);

            if (w <= 0 || h <= 0 || w > MAX_W || h > MAX_H || len != w * h) {
                printf("[AI-G NET] invalid frame w=%d h=%d len=%d\n", w, h, len);
                break;
            }

            uint8_t *buf = (uint8_t *)malloc(len);
            if (!buf) break;

            if (read_n(c, buf, len) < 0) {
                free(buf);
                break;
            }

            process_frame(buf, w, h, seq);
            free(buf);
        }

        close(c);
        printf("[AI-G NET] client disconnected\n");
        fflush(stdout);
    }

    return 0;
}

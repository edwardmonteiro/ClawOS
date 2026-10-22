/* ClawOS Native v0.2: original 32-bit freestanding kernel.
 * No Linux, libc, Node.js, or existing OS runs on the target.
 * VGA text and PS/2-compatible keyboard via BIOS legacy emulation.
 * Agent commands are a deterministic, offline planner; NOT an LLM.
 * This demo has NO disk filesystem, network, persistent storage, or OpenClaw runtime.
 */
#include <stdint.h>
#include <stddef.h>
#define W 80
#define H 25
#define MAX_TASKS 8
static volatile uint16_t *const vga=(uint16_t *)0xB8000;
static unsigned row=0,col=0;
static uint8_t attr=0x07;
static char command[160];
static unsigned cmd_len=0;
struct task {char request[72];char skill[24]; uint8_t state;};
static struct task tasks[MAX_TASKS];
static unsigned task_count=0;
static int pending=-1;
static int keystate_shift=0;
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1, %0":"=a"(v):"Nd"(p)); return v; }
static inline void outb(uint16_t p,uint8_t v) {__asm__ volatile("outb %0, %1"::"a"(v),"Nd"(p));}
static void move_cursor(void){uint16_t pos=(uint16_t)(row*W+col);outb(0x3D4,14);outb(0x3D5,(uint8_t)(pos>>8));outb(0x3D4,15);outb(0x3D5,(uint8_t)pos);}
static void scroll(void){if(row<H)return;for(unsigned y=1;y<H;y++)for(unsigned x=0;x<W;x++)vga[(y-1)*W+x]=vga[y*W+x];for(unsigned x=0;x<W;x++)vga[(H-1)*W+x]=(uint16_t)(0x0720);row=H-1;}
static void putc(char c){if(c=='\n'){col=0;row++;scroll();move_cursor();return;}if(c=='\r'){col=0;move_cursor();return;}if(c=='\b'){if(col){col--;vga[row*W+col]=((uint16_t)attr<<8)|' ';}move_cursor();return;}if(col>=W){col=0;row++;scroll();}vga[row*W+col]=((uint16_t)attr<<8)|(uint8_t)c;col++;move_cursor();}
static void puts(const char *s){while(*s)putc(*s++);}
static void line(const char *s){puts(s);putc('\n');}
static void clear(void){for(unsigned i=0;i<W*H;i++)vga[i]=0x0720;row=col=0;move_cursor();}
static void setcolor(uint8_t a){attr=a;}
static int eq(const char*a,const char*b){while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static int starts(const char*a,const char*b){while(*b){if(*a++!=*b++)return 0;}return 1;}
static int contains(const char*hay,const char*needle){for(;*hay;hay++)if(starts(hay,needle))return 1;return 0;}
static void copy(char*dst,const char*src,unsigned max){unsigned i=0;while(src[i]&&i+1<max){dst[i]=src[i];i++;}dst[i]=0;}
static void putdec(uint32_t n){char b[12];unsigned i=0;do{b[i++]=(char)('0'+n%10);n/=10;}while(n&&i<sizeof(b));while(i)putc(b[--i]);}
static void banner(void){
  setcolor(0x0B);
  line("========================================");
  line("           CLAWOS NATIVE v0.2");
  line("========================================");
  setcolor(0x07);
  line("x86 bare metal | kernel proprio | offline");
  line("'help' para comandos. Sem rede ou LLM.");
  line("");
}
static void prompt(void){setcolor(0x0A);puts("claw> ");setcolor(0x07);}
static void hardware(void){uint32_t a,b,c,d;char vendor[13];__asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0));((uint32_t*)vendor)[0]=b;((uint32_t*)vendor)[1]=d;((uint32_t*)vendor)[2]=c;vendor[12]=0;puts("CPU vendor: ");line(vendor);line("ISA kernel: x86 32-bit protected mode");line("Display: VGA text (80x25)");line("Keyboard: PS/2 scancode 1 (ou USB legacy BIOS)");line("RAM: nao detectada no prototipo; 4GB nao foi verificado");line("Boot: BIOS/CSM por disquete emulado");}
static void skill_from(const char*request,char*skill){if(contains(request,"arquiv")||contains(request,"file"))copy(skill,"files",24);else if(contains(request,"diagnos")||contains(request,"hardware"))copy(skill,"diagnostics",24);else if(contains(request,"app")||contains(request,"codigo"))copy(skill,"development",24);else if(contains(request,"pesquis")||contains(request,"search"))copy(skill,"research",24);else copy(skill,"general",24);}
static void addtask(const char*request){if(!*request){line("Uso: agent <pedido sem acentos>");return;}if(task_count>=MAX_TASKS){line("Fila cheia (8). Reinicie para limpar RAM.");return;}struct task*t=&tasks[task_count];copy(t->request,request,sizeof(t->request));skill_from(request,t->skill);t->state=0;pending=(int)task_count;puts("Plano #");putdec(task_count+1);puts(" | skill=");line(t->skill);puts("Objetivo: ");line(t->request);line("Etapas: interpretar -> aprovar -> executar quando houver drivers");line("Status: AGUARDANDO APROVACAO. Nao executa acoes externas.");line("Use 'approve' ou 'reject'.");task_count++;}
static void listtasks(void){if(!task_count){line("Nenhuma tarefa em RAM.");return;}for(unsigned i=0;i<task_count;i++){puts("#");putdec(i+1);puts(" [");puts(tasks[i].state==0?"PENDING":tasks[i].state==1?"APPROVED/NOT RUN":"REJECTED");puts("] ");line(tasks[i].request);}}
static void process(const char*s){
 if(eq(s,"help")){line("help         comandos disponiveis");line("about        arquitetura e limites");line("status       diagnostico do kernel");line("hardware     CPU / console / boot");line("skills       catalogo de capacidades planejadas");line("agent TEXTO  cria plano local em RAM");line("tasks        lista tarefas");line("approve      aprova plano; NAO executa");line("reject       rejeita ultimo plano");line("clear        limpa a tela");line("reboot       reinicia a maquina");return;}
 if(eq(s,"clear")){clear();return;}
 if(eq(s,"about")){line("ClawOS e um kernel experimental escrito do zero em C+Assembly.");line("Nao executa Linux/macOS/Node/OpenClaw. Usa firmware BIOS ao carregar.");line("Agente inicial: roteador de intencoes offline, sem IA generativa.");return;}
 if(eq(s,"hardware")){hardware();return;}
 if(eq(s,"status")){line("Kernel: ONLINE | VGA: ONLINE | teclado: polling");puts("Tasks: ");putdec(task_count);puts("/");putdec(MAX_TASKS);putc('\n');line("Filesystem: NOT IMPLEMENTED | Network: NOT IMPLEMENTED");line("OpenClaw gateway: NOT CONNECTED | LLM: NOT CONNECTED");return;}
 if(eq(s,"skills")){line("diagnostics: parcial (CPUID)");line("files: planejada, requer FAT driver");line("development: planejada, requer processos/toolchain");line("research: planejada, requer rede + LLM");line("general: roteamento local, sem inferencia de IA");return;}
 if(starts(s,"agent ")){addtask(s+6);return;}
 if(eq(s,"tasks")){listtasks();return;}
 if(eq(s,"approve")){if(pending<0){line("Nao ha plano aguardando.");return;}tasks[pending].state=1;pending=-1;line("Plano aprovado, NAO executado. Sem drivers de acao.");return;}
 if(eq(s,"reject")){if(pending<0){line("Nao ha plano aguardando.");return;}tasks[pending].state=2;pending=-1;line("Plano rejeitado.");return;}
 if(eq(s,"reboot")){line("Reiniciando...");outb(0x64,0xFE);for(;;)__asm__ volatile("hlt");}
 if(!*s)return;
 line("Comando desconhecido. Digite 'help'.");
}
/* US PC AT scancode set 1. Avoid accents until layout driver exists. */
static const char unshift[128]={
 [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',[0x08]='7',[0x09]='8',[0x0a]='9',[0x0b]='0',[0x0c]='-',[0x0d]='=',
 [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',[0x1a]='[',[0x1b]=']',
 [0x1e]='a',[0x1f]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',
 [0x2b]='\\',[0x2c]='z',[0x2d]='x',[0x2e]='c',[0x2f]='v',[0x30]='b',[0x31]='n',[0x32]='m',[0x33]=',',[0x34]='.',[0x35]='/',[0x39]=' '};
static const char shifted[128]={
 [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',[0x07]='^',[0x08]='&',[0x09]='*',[0x0a]='(',[0x0b]=')',[0x0c]='_',[0x0d]='+',
 [0x1a]='{',[0x1b]='}',[0x27]=':',[0x28]='"',[0x2b]='|',[0x33]='<',[0x34]='>',[0x35]='?'};
static char upper(char c){return c>='a'&&c<='z'?(char)(c-32):c;}
static void key(uint8_t scan){if(scan==0x2a||scan==0x36){keystate_shift=1;return;}if(scan==0xaa||scan==0xb6){keystate_shift=0;return;}if(scan&0x80)return;
 if(scan==0x1c){command[cmd_len]=0;putc('\n');process(command);cmd_len=0;prompt();return;}
 if(scan==0x0e){if(cmd_len){cmd_len--;putc('\b');}return;}
 if(scan>=128)return;
 char ch=unshift[scan];
 if(keystate_shift){if(shifted[scan])ch=shifted[scan];else ch=upper(ch);}
 if(!ch)return;
 if(cmd_len+1<sizeof(command)){command[cmd_len++]=ch;putc(ch);}
}
void kernel_main(void){clear();banner();prompt();for(;;){if((inb(0x64)&1)!=0){uint8_t scan=inb(0x60);key(scan);}else{__asm__ volatile("pause");}}}

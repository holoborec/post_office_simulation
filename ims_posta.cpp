/**************************************************************
					Projekt do predmetu IMS 
					Nazov: POSTA
					Autori: Dominik Holec, Tomas Hnat
					Loginy: xholec07, xhnatt00
**************************************************************/


#include "simlib.h"
#include <stdio.h>
#include <iostream>
#include <sstream>  
using namespace std;

//Program pracuje v hodinach, ukony namerane v minutach ci sekundach su preto prevadzane na hodiny

//V statistike je vhodne uviest namerane veliciny v spravnych mierach, preto bude treba z hodin previest spat 
//na minuty resp. sekundy
#define MINUT * 60
#define SEKUND * 3600

//Definicia dlzky experimentu - pracovna doba PO-PA 8:00 - 19:00
const double ZACIATOK_PRAC_DOBY = 8.0;
const double KONIEC_PRAC_DOBY = 19.0;
//Stredna doba medzi prichodmi zakaznikov je 35 sekund
const double CAS_PRICHODOV = 35.0/3600.0;
//Zakaznici sa delia do zadanych skupin v pomere 10:9:41:28:12 (vid. dokumentacia) 
const double CAS_ZAKAZNIK_ODOSL_BALIK = CAS_PRICHODOV/0.10;
const double CAS_ZAKAZNIK_PRIJAT_BALIK = CAS_PRICHODOV/0.09;
const double CAS_ZAKAZNIK_LIST_OP = CAS_PRICHODOV/0.41;
const double CAS_ZAKAZNIK_PENAZ_OP = CAS_PRICHODOV/0.28;
const double CAS_ZAKAZNIK_OST_OP = CAS_PRICHODOV/0.12;
//Otvorenych prepazok je 6, z toho 2 su len na baliky, ostatne univerzalne 
//Celkovo posta disponuje 8 prepazkami, pomer univerzalnych a balikovych moze byt pravdepodobne lubovolny
const int POCET_UNIVERZALNYCH_PREPAZOK = 4;
const int POCET_BALIKOVYCH_PREPAZOK = 2; 
//Vystupny subor urceny na ulozenie statistik
const string VYSTUPNY_SUBOR = "posta.out";
//Vydanie vyvolavacich listkov trva 15s exponencialne (1/4 minuty na hodiny)
const double CAS_VYDAJ_LISTKOV = 0.25/60.0;
//Vzhladom na to ze Exp(15s) vie vygenerovat aj 0.005s, co nie je realna doba prevzatia listku, minimalnu 
//dobu prevzatia vyvolavacieho listku som urcil na 2 sekundy
const double MINIMALNY_CAS_VYDAJA = 2.00/3600.0;
const double CAS_OBEDNA_PRESTAVKA = 30.0/60.0;


//Ladiace vypisy, mozme zistit kolko zakaznikov patri do akych skupin, v maine si vytlacime konecne pocty
long int zak_odosl = 0;
long int zak_prij = 0;
long int zak_list = 0; 
long int zak_pen = 0;
long int zak_ost = 0;

//Globalne deklaracie objektov
Facility univ_prepazka[POCET_UNIVERZALNYCH_PREPAZOK];
Facility balik_prepazka[POCET_BALIKOVYCH_PREPAZOK];
Facility vydaj_listkov;

//Statistiky typu Stat 
//Definovane, ale aktualne nepouzivane, mozne odkomentovat vypisy v maine
//Funkciu prevzali statistiky typu Histogram obsahujuce tie iste udaje + histogram
Stat Systemova_Doba;
Stat Vydanie_listkov;
Stat Odoslanie_balikov;
Stat Prijem_balikov;
Stat Listove_operacie; 
Stat Penazne_operacie;
Stat Ostatne_operacie;
Stat Timedout; 

//Statisticke objekty typu histogram urcene na zber uzitocnych dat a vytlacenie do vystupneho suboru
Histogram Systemova_Doba2("Histogram doba zakaznika v systeme [min]", 0, 2, 30);
Histogram Vydanie_listkov2("Histogram vydavanie listkov [sec]", 2, 5, 25);
Histogram Odoslanie_balikov2("Histogram odoslania balikov [min]", 0, 2, 30);
Histogram Prijem_balikov2("Histogram prijimania balikov [min]", 0, 2, 30);
Histogram Listove_operacie2("Histogram listovych operacii [min]", 0, 2, 30); 
Histogram Penazne_operacie2("Histogram penaznych operacii [min]", 0, 2, 30);
Histogram Ostatne_operacie2("Histogram ostatnych operacii [min]", 0, 2, 30);
Histogram Timedout2("Histogram netrpezlivych poziadavkov [min]", 0, 2, 30);

//Funkcia na prevod int na string 
//Prevzate z mojho projektu do predmetu ISA 
string IntToString (int a)
{
    ostringstream temp;
    temp<<a;
    return temp.str();
}

//Udalost Timeout
//Kod inspirovany suborom model2-timeout.cc zo zlozky examples v kniznici simlib
//Udalost sluzi na implementovanie netrpezliveho chovania zakaznikov, ktori po istom case zo systemu odidu 
//aj za cenu nevybavenia poziadavkov
//Na vstup tato trieda dostava ukazatel na proces, ktory o timeout ziada a cas, po ktorom ma byt zruseny
//Vysledkom prebehnutia funkcie behavior je zrusenie procesu 
//POZOR: proces ziadajuci o Timeout nemusi byt nutne zruseny - v pripade, ze sa dostane k zariadeniu, o ktore 
//ziadal, zrusi ziadost o timeout, casovac sa vypne a proces pokracuje dalej v systeme 
class Timeout : public Event {
    Process *ptr;               // ukazatel na proces, ktory si vynutil zapnutie casovacu
  public:
    double doba;                //premenna uchovavajuca dlzku timeoutu pre statisticke ucely
    Timeout(double t, Process *p): ptr(p) { //Konstruktor
	  doba = t;
      Activate(Time+t);         //prikazom activate sa skoci do funkcie Behavior v case o t jednotiek 
    }
    void Behavior() { //V tejto funkcii sa proces rusi - odchadza z fronty v ktorej stoji a opusta system
	  Timedout(this->doba MINUT); //Zaznamenanie dlzky timeoutu do statistik
	  Timedout2(this->doba MINUT);
      delete ptr;               // vymazanie procesu
    }
};


//Trieda ObednaPrestavka sluzi na modelovanie 30 minutoveho obedu. 
//Vstupnymi argumentami su cas, na kedy je obedna prestavka naplanovana a factype resp. facnumber 
//reprezentujuce konkretnu prepazku, ktorej pani zamestnankyna si dopraje polhodinku na obed
//Vysledkom procesu je polhodinova pauza danej prepazky, ktora nastava ihned, pokial prepazku nikto v 
//danom case nepouziva, alebo po dokonceni prave prebiehajucej transakcie (priorita procesu)
class ObednaPrestavka : public Process {
	public: 
	double obedna_prestavka; //cas, kedy sa ma zacat obedna prestavka
	int factype; //type 0 = univ_prepazka, type 1 = balik_prepazka
	int facnumber; //cislo konkretnej prepazky
	
	ObednaPrestavka(double doba, int typ, int poradie){ //konstruktor
		obedna_prestavka = doba;
		factype = typ;
		facnumber = poradie;
		Activate(obedna_prestavka); //proces aktivujeme v case obedna_prestavka (ten je generovany vo funkcii GeneratorObedov)
	}
	
	//factype + facnumber je jednoznacna identifikacia, o ake zariadenie sa jedna
	//Proces ObednaPrestavka s PRIORITOU PROCESU (zaradi sa na zaciatok fronty) obsadi dane zariadenie a na 30 min ho "vyradi z obsluhy" 
	void Behavior(){
		Priority = 1; //nastavenie vyssej priority tohto procesu voci zakaznickym procesom (implicitna priorita = 0, nizsia)
		if(factype == 0){
			Seize(univ_prepazka[facnumber]);
			Wait(CAS_OBEDNA_PRESTAVKA);
			Release(univ_prepazka[facnumber]);
		}
		else{
			Seize(balik_prepazka[facnumber]);
			Wait(CAS_OBEDNA_PRESTAVKA);
			Release(balik_prepazka[facnumber]);
		}
		
	}
	
	
	
};

//"Abstraktna" trieda zakaznik, ktorej vlastnosti zdielaju konkretne triedy zakaznikov
//Spolocnymi parametrami pre vsetky triedy implementujuce toto "rozhranie" je prichod do systemu, doba vydania listku 
//a doba v systeme, vsetky implicitne inicializovane na hodnotu 0
class Zakaznik : public Process {
 protected:
   double prichod;  // cas prichodu procesu do systemu
   double doba_vydania_listku; //cas, za ktory sa procesu podarilo ziskat vyvolavaci listok
   double doba_v_systeme; //cas, ktory proces stravil v systeme
 public:
   Zakaznik() : prichod(0), doba_vydania_listku(0), doba_v_systeme(0) { Activate(); }
};

//Tato trieda modeluje zakaznika, ktory prisiel odoslat balik
//Vyzdvihnutie listka trva 2s + Exp(15s), odoslanie balika 4-7min rovnomerne a pripadny timeout nastavujeme na 5min + Exp(5min)
class Zakaznik_odosl_balik : public Zakaznik { 
   void Behavior() {
	   bool casovac = false; //premenna sluziaca na zistenie, ci bol spusteny casovac/timeout
	   Event *timeout = NULL; //timeout implicitne nenastavujeme
	   prichod = Time; //zaznamename si cas prichodu do systemu
	   Seize(vydaj_listkov); //vyberieme si listok (pripadne sa zaradime do fronty)
	   doba_vydania_listku = Exponential(CAS_VYDAJ_LISTKOV) + MINIMALNY_CAS_VYDAJA; //najdeme konkretnu poziadavku (trva nejaky cas)
	   Wait(doba_vydania_listku); 
	   Release(vydaj_listkov); //uvolnime zariadenie dalsim zakaznikom
	   Vydanie_listkov(doba_vydania_listku SEKUND);
	   Vydanie_listkov2(doba_vydania_listku SEKUND);
	   int a = 0; //vyberieme prepazku pre nas urcenu s najmensou moznou frontou
	   for (int i = 0; i < POCET_BALIKOVYCH_PREPAZOK; i++){
		  if(balik_prepazka[i].QueueLen() < balik_prepazka[a].QueueLen())
			a = i;
	   }
	   if(Random() < 0.02){ //2% zakaznikov su netrpezlivi
		   casovac = true;
		   double cas = Exponential(5.0/60.0) + 5.0/60.0; //cas 5min + Exp(5min)
		   timeout = new Timeout(cas, this); //nastavime casovac na zvoleny cas
	   }
	   Seize(balik_prepazka[a]); //pokusime sa ziskat zariadenie
	   if(casovac){ //ak sme sa dostali sem, znamena to ziskanie zariadenia pred uplynutim casovaca - rusime casovac 
			delete timeout;
	   }
	   double doba_obsluhy_odosl = Uniform(4.0/60.0, 7.0/60.0); //4-7 minut odoslanie balika
	   Wait(doba_obsluhy_odosl);
	   Release(balik_prepazka[a]); //odchod od prepazky
	   doba_v_systeme = Time - prichod; //statisticke udaje
	   Systemova_Doba(doba_v_systeme MINUT);
	   Systemova_Doba2(doba_v_systeme MINUT);
	   Odoslanie_balikov(doba_v_systeme MINUT);
	   Odoslanie_balikov2(doba_v_systeme MINUT); 
       zak_odosl++;
   } //Behavior
 public:
   static void Create() { new Zakaznik_odosl_balik; } //metoda create
};

/* PROSIM POZOR: Triedy Zakaznik_prijat_balik, Zakaznik_list_op, Zakaznik_penaz_op a Zakaznik_ostat_op NEBUDU KOMENTOVANE. 
                 Algoritmy a principy tu su totozne s funkcionalitou v triede Zakaznik_odosl_balik, pre info hladajte tam. 
				 Pripadne komentare by mohli zneprehladnovat kod.
				 Jedinym rozdielom u danych tried moze byt trvanie danej poziadavky a vyuzita prepazka*/



//trieda zakaznika, ktory si prisiel vyzdvihnut balik
//Vyzdvihnutie balika trva 2-3 minuty rovnomerne, pouziva sa balikova prepazka
class Zakaznik_prijat_balik : public Zakaznik { 
   void Behavior() {
	   bool casovac = false;
	   Event *timeout = NULL;
	   prichod = Time;
	   Seize(vydaj_listkov);
	   doba_vydania_listku = Exponential(CAS_VYDAJ_LISTKOV) + MINIMALNY_CAS_VYDAJA;
	   Wait(doba_vydania_listku);
	   Release(vydaj_listkov);
	   Vydanie_listkov(doba_vydania_listku SEKUND);
	   Vydanie_listkov2(doba_vydania_listku SEKUND);
	   int a = 0;
	   for (int i = 0; i < POCET_BALIKOVYCH_PREPAZOK; i++){
		  if(balik_prepazka[i].QueueLen() < balik_prepazka[a].QueueLen())
			a = i;
	   }
	   if(Random() < 0.02){
		   casovac = true;
		   double cas = Exponential(5.0/60.0) + 5.0/60.0; 
		   timeout = new Timeout(cas, this);
	   }
	   Seize(balik_prepazka[a]);
	   if(casovac){
			delete timeout;
	   }
	   double doba_obsluhy_prij = Uniform(2.0/60.0, 3.0/60.0);
	   Wait(doba_obsluhy_prij);
	   Release(balik_prepazka[a]);
	   doba_v_systeme = Time - prichod;
	   Systemova_Doba(doba_v_systeme MINUT);
	   Systemova_Doba2(doba_v_systeme MINUT);
	   Prijem_balikov(doba_v_systeme MINUT);
	   Prijem_balikov2(doba_v_systeme MINUT);
	   zak_prij++;
   } //Behavior
 public:
   static void Create() { new Zakaznik_prijat_balik; }
};

//trieda zakaznika, ktory prisiel prevziat/odoslat list
//prevzatie/odoslanie listu trva 1-2 minuty rovnomerne, pouziva sa univerzalna prepazka
class Zakaznik_list_op : public Zakaznik { // vnějąí poľadavky
   void Behavior() {
	   bool casovac = false;
	   Event *timeout = NULL;
	   prichod = Time;
	   Seize(vydaj_listkov);
	   doba_vydania_listku = Exponential(CAS_VYDAJ_LISTKOV) + MINIMALNY_CAS_VYDAJA;
	   Wait(doba_vydania_listku);
	   Release(vydaj_listkov);
	   Vydanie_listkov(doba_vydania_listku SEKUND);
	   Vydanie_listkov2(doba_vydania_listku SEKUND);
	   int a = 0;
	   for (int i = 0; i < POCET_UNIVERZALNYCH_PREPAZOK; i++){
		  if(univ_prepazka[i].QueueLen() < univ_prepazka[a].QueueLen())
			a = i;
	   }
	   if(Random() < 0.02){
		   casovac = true; 
		   double cas = Exponential(5.0/60.0) + 5.0/60.0; 
		   timeout = new Timeout(cas, this);
	   }
	   Seize(univ_prepazka[a]);
	   if(casovac){
			delete timeout;
	   }
	   double doba_obsluhy_list = Uniform(1.0/60.0, 2.0/60.0);
	   Wait(doba_obsluhy_list);
	   Release(univ_prepazka[a]);
	   doba_v_systeme = Time - prichod;
	   Systemova_Doba(doba_v_systeme MINUT);
	   Systemova_Doba2(doba_v_systeme MINUT);
	   Listove_operacie(doba_v_systeme MINUT);
	   Listove_operacie2(doba_v_systeme MINUT);
	   zak_list++;
   } 
 public:
   static void Create() { new Zakaznik_list_op; }
};

//trieda zakaznika, ktory prisiel vykonat penaznu operaciu
//penazna operacia trva 1-2 minuty rovnomerne (v zavislosti na pocte faktur), pouziva sa univerzalna prepazka
class Zakaznik_penaz_op : public Zakaznik { // vnějąí poľadavky
   void Behavior() {
	   bool casovac = false;
	   Event *timeout = NULL;
	   prichod = Time;
	   Seize(vydaj_listkov);
	   doba_vydania_listku = Exponential(CAS_VYDAJ_LISTKOV) + MINIMALNY_CAS_VYDAJA;
	   Wait(doba_vydania_listku);
	   Release(vydaj_listkov);
	   Vydanie_listkov(doba_vydania_listku SEKUND);
	   Vydanie_listkov2(doba_vydania_listku SEKUND);
	   int a = 0;
	   for (int i = 0; i < POCET_UNIVERZALNYCH_PREPAZOK; i++){
		  if(univ_prepazka[i].QueueLen() < univ_prepazka[a].QueueLen())
			a = i;
	   }
	   if(Random() < 0.02){
		   casovac = true;
		   double cas = Exponential(5.0/60.0) + 5.0/60.0; 
		   timeout = new Timeout(cas, this);
	   }
	   Seize(univ_prepazka[a]);
	   if(casovac){
			delete timeout;
	   }
	   double doba_obsluhy_penaz = Uniform(2.5/60.0, 6.0/60.0);
	   Wait(doba_obsluhy_penaz);
	   Release(univ_prepazka[a]);
	   doba_v_systeme = Time - prichod;
	   Systemova_Doba(doba_v_systeme MINUT);
	   Systemova_Doba2(doba_v_systeme MINUT);
	   Penazne_operacie(doba_v_systeme MINUT);
	   Penazne_operacie2(doba_v_systeme MINUT);
	   zak_pen++;
   } //Behavior
 public:
   static void Create() { new Zakaznik_penaz_op; }
};

//trieda zakaznika, ktory prisiel vykonat nejake dalsie operacie (vyber dochodku, spytat sa na sporenie...)
//takato operacia bezne trva 1-5 minut rovnomerne, pouziva sa univerzalna prepazka
class Zakaznik_ostat_op : public Zakaznik { // vnějąí poľadavky
   void Behavior() {
	   bool casovac = false;
	   Event *timeout = NULL;
	   prichod = Time;
	   Seize(vydaj_listkov);
	   doba_vydania_listku = Exponential(CAS_VYDAJ_LISTKOV) + MINIMALNY_CAS_VYDAJA;
	   Wait(doba_vydania_listku);
	   Release(vydaj_listkov);
	   Vydanie_listkov(doba_vydania_listku SEKUND);
	   Vydanie_listkov2(doba_vydania_listku SEKUND);
	   int a = 0;
	   for (int i = 0; i < POCET_UNIVERZALNYCH_PREPAZOK; i++){
		  if(univ_prepazka[i].QueueLen() < univ_prepazka[a].QueueLen())
			a = i;
	   }
	   if(Random() < 0.02){
		   casovac = true;
		   double cas = Exponential(5.0/60.0) + 5.0/60.0; 
		   timeout = new Timeout(cas, this);
	   }
	   Seize(univ_prepazka[a]);
	   if(casovac){
			delete timeout;
	   }
	   double doba_obsluhy_ost = Uniform(1.0/60.0, 5.0/60.0);
	   Wait(doba_obsluhy_ost);
	   Release(univ_prepazka[a]);
	   doba_v_systeme = Time - prichod;
	   Systemova_Doba(doba_v_systeme MINUT);
	   Systemova_Doba2(doba_v_systeme MINUT);
	   Ostatne_operacie(doba_v_systeme MINUT);
	   Ostatne_operacie2(doba_v_systeme MINUT);
		zak_ost++;
   } //Behavior
 public:
   static void Create() { new Zakaznik_ostat_op; }
};


//Generator sluziaci na vytvorenie roznych poziadavkov
//Kod PREVZATY z centrala.cc zo zlozky examples kniznice simlib
typedef void (*CreatePtr_t)();         // typ ukazatel na statickou metodu
class Generator : public Event {       // generátor vnějsích poľadavků
   CreatePtr_t create;  // ukazatel na metodu Create()
   double dt;           // interval mezi vytvořením poľadavků
   void Behavior() {
      create();                        // generování poľadavku
      Activate(Time+Exponential(dt));  // daląí poľadavek
   }
 public:
   Generator(CreatePtr_t p, double _dt) : create(p), dt(_dt) {
     Activate();
   }
};

//Generator, ktory zabezpeci pre kazdu jednu otvorenu prepazku cas obednej prestavky v dobe 11:30 - 14:30 
//Vygeneruje sa zaciatocny cas obednej prestavky rovnomernym rozlozenim. Prestavka trva 0:30, preto posledny mozny cas 
//zaciatku obednej prestavky je 14:00
//Nenachadza sa tu ziadne Activate - tento proces bezi len raz, vygeneruje vsetky procesy v cykloch for
class GeneratorObedov : public Event {
	
	void Behavior(){
		for(int i = 0; i < POCET_UNIVERZALNYCH_PREPAZOK; i++){ //1 proces pre kazdu univerzalnu prepazku
			double zaciatok_obednej_prestavky = Uniform(11.50, 14.0);
			(new ObednaPrestavka(zaciatok_obednej_prestavky, 0, i))->Activate(); //0 reprezentuje "typ univerzalna prepazka", i jej poradie
		}
		for(int j = 0; j < POCET_BALIKOVYCH_PREPAZOK; j++){ //1 proces pre kazdu balikovu prepazku
			double zaciatok_obednej_prestavky = Uniform(11.50, 14.0);
			(new ObednaPrestavka(zaciatok_obednej_prestavky, 1, j))->Activate(); //1 reprezentuje "typ balikova prepazka", j jej poradie
		}
	}
	
};


int main(){
	
	SetOutput(VYSTUPNY_SUBOR.c_str()); //nastavenie vystupneho suboru
	
	for(int exp = 1; exp <= 10; exp++){
		
		Init(ZACIATOK_PRAC_DOBY, KONIEC_PRAC_DOBY); //nastavenie pracovnej doby a zaroven modeloveho casu od 8:00 do 19:00 
		
		Systemova_Doba.Clear();
		Vydanie_listkov.Clear();
		Odoslanie_balikov.Clear();
		Prijem_balikov.Clear();
		Listove_operacie.Clear(); 
		Penazne_operacie.Clear();
		Ostatne_operacie.Clear();
		Timedout.Clear(); 
		
		Vydanie_listkov2.Clear();
		Odoslanie_balikov2.Clear();
		Prijem_balikov2.Clear();
		Listove_operacie2.Clear();
		Penazne_operacie2.Clear();
		Ostatne_operacie2.Clear();
		Systemova_Doba2.Clear();
		Timedout2.Clear();
		for(int i = 0; i < POCET_UNIVERZALNYCH_PREPAZOK; i++){
			univ_prepazka[i].Clear();
		}
		for(int j = 0; j < POCET_BALIKOVYCH_PREPAZOK; j++){
			balik_prepazka[j].Clear();
		}
		vydaj_listkov.Clear();
		
		
		//generovanie procesov konkretnych zakaznikov
		new Generator(Zakaznik_odosl_balik::Create, CAS_ZAKAZNIK_ODOSL_BALIK);
		new Generator(Zakaznik_prijat_balik::Create, CAS_ZAKAZNIK_PRIJAT_BALIK);
		new Generator(Zakaznik_list_op::Create, CAS_ZAKAZNIK_LIST_OP);
		new Generator(Zakaznik_penaz_op::Create, CAS_ZAKAZNIK_PENAZ_OP);
		new Generator(Zakaznik_ostat_op::Create, CAS_ZAKAZNIK_OST_OP);
		//generovanie casov obednych prestavok konkretnych prepazok
		(new GeneratorObedov)->Activate();
		//spustenie simulacie
		Run();
		
		//Vytlacenie poctu zastupcov danych skupin zakaznikov	
		/*
		cout << zak_odosl << endl;
		cout << zak_prij << endl;
		cout << zak_list << endl;
		cout << zak_pen << endl;
		cout << zak_ost << endl; */
		
		Print("Experiment cislo %i \n", exp);
		
		//Statistiky typu Stat
		/*
		Print("Doba vydania vyvolavacich listkov v sekundach: \n");
		Vydanie_listkov.Output(); //Output v sekundach
		Print("Dlzka navstevy pobocky zakaznika odosielajuceho balik v minutach: \n");
		Odoslanie_balikov.Output(); //Output v minutach
		Print("Dlzka navstevy pobocky zakaznika prijimajuceho balik v minutach: \n");
		Prijem_balikov.Output(); //Output v minutach
		Print("Dlzka navstevy pobocky zakaznika s listovymi operaciami v minutach: \n");
		Listove_operacie.Output(); //Output v minutach
		Print("Dlzka navstevy pobocky zakaznika s penaznymi operaciami v minutach: \n");
		Penazne_operacie.Output(); //Output v minutach
		Print("Dlzka navstevy pobocky zakaznika s ostatnymi operaciami v minutach: \n");
		Ostatne_operacie.Output(); //Output v minutach
		Print("Doba zakaznika v systeme v minutach: \n");
		Systemova_Doba.Output(); //Output v minutach
		Print("Doba timeoutov v minutach a ich pocet: \n");
		Timedout.Output();  //Output v minutach */
		
		//Statistiky typu Histogram, zapis do suboru
		Vydanie_listkov2.Output(); //Output v sekundach
		/*
		Odoslanie_balikov2.Output(); //Output v minutach
		Prijem_balikov2.Output(); //Output v minutach
		Listove_operacie2.Output(); //Output v minutach
		Penazne_operacie2.Output(); //Output v minutach
		Ostatne_operacie2.Output(); //Output v minutach */
		Systemova_Doba2.Output(); //Output v minutach
		Timedout2.Output(); //Output v minutach
		
		//Tlacenie statistik vystupu zariadeni univerzalnych prepazok
		for(int i = 0; i < POCET_UNIVERZALNYCH_PREPAZOK; i++){
			string to_print = "Univerzalna prepazka c. " + IntToString(i) + " stats v [hod]\n";
			Print(to_print.c_str());
			univ_prepazka[i].Output();
		}
		//Tlacenie statistik vystupu zariadeni balikovych prepazok
		for(int j = 0; j < POCET_BALIKOVYCH_PREPAZOK; j++){
			string to_print = "Balikova prepazka c. " + IntToString(j) + "stats v [hod] \n";
			Print(to_print.c_str());
			balik_prepazka[j].Output();
		}
		Print("Stroj na vydavanie vyvolavacich listkov stats [min]\n");
		vydaj_listkov.Output();
		
		SIMLIB_statistics.Output();
		
	}
	
	
	
	
}
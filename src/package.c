#include <notstd/core.h>
#include <notstd/str.h>
#include <notstd/threads.h>

#include <auror/archive.h>
#include <auror/config.h>
#include <auror/database.h>
#include <auror/inutility.h>

#include <cryptominisat5/cryptominisat_c.h>

typedef struct solver{
	SATSolver* sat;
	desc_s**   var;
}solver_s;

__private char* tabs = "                                                                                     ";

solver_s* sat_ctor(solver_s* s){
	s->sat = cmsat_new();
	cmsat_set_num_threads(s->sat, 1);
	if( !s->sat ) die("internal error, unable to initializate cryptominisat");
	s->var = MANY(desc_s*, 128);
	return s;
}

void sat_dtor(void* ps){
	solver_s* s = ps;
	mem_free(s->var);
	cmsat_free(s->sat);
}

unsigned sat_var(solver_s* s, desc_s* d){
	if( d->link ) d = d->link;
	if( d->var == -1 ){
	    cmsat_new_vars(s->sat, 1);
		unsigned id = mem_ipush(&s->var);
		s->var[id] = d;
		d->var = id;
	}
	return d->var;
}

c_Lit sat_lit(unsigned var, bool negative) {
	c_Lit lit;
	lit.x = (var << 1) | negative;
	return lit;
}

char* ctab(unsigned tmp){
	unsigned len = strlen(tabs);
	tmp *= 2;
	if( tmp > len ) tmp = 0;
	else tmp = len - tmp;
	return &tabs[tmp];
}

#define MAX_CLAUSE 128

__private void dependency_clauses(solver_s* s, arch_s* arch, desc_s* desc, unsigned tab) {
	if( !desc->depends ) return;
	if( desc->conflicts ){
	
	}
	dbg_info("%sdependency of %s", ctab(tab),desc->name);
	const unsigned var = sat_var(s, desc);
	mforeach(desc->depends, i){
		dbg_info("%sdepends %s", ctab(tab),desc->depends[i].name);
		desc_s* candidates = database_sync_find(arch->sync, desc->depends[i].name);
		if( !candidates ) die("unable to solve dependency %s required by %s", desc->depends[i].name, desc->name);
		c_Lit clause[MAX_CLAUSE];
		unsigned nc = 0;
		clause[nc++] = sat_lit(var, 1);
		int foundCandidate = 0;
		ldforeach(candidates, candy){
			dbg_info("%scheck candydate: %s 0x%X %s", ctab(tab), candy->name, desc->depends[i].flags, desc->depends[i].version);
			if( desc_accept_version(candy, desc->depends[i].flags, desc->depends[i].version) ){
				const unsigned varcandy = sat_var(s, candy);
				if( nc >= MAX_CLAUSE ) die("internal error, required more than %u clause, please report this issue", MAX_CLAUSE);
				clause[nc++] = sat_lit(varcandy, 0);
				foundCandidate = 1;
				dbg_info("%s++%s", ctab(tab), candy->name);
				if( !(candy->flags & DESC_FLAG_CROSS) ){
					candy->flags |= DESC_FLAG_CROSS;
					dependency_clauses(s, arch, candy, tab+1);
				}
			}
			else{
				dbg_info("%s##%s.%s unmatch %s.%s", ctab(tab), candy->name, candy->version, desc->depends[i].name, desc->depends[i].version);
			}
		}
		if( !foundCandidate ) die("unable to find candidate to solve dependency %s required by %s", desc->depends[i].name, desc->name);
		cmsat_add_clause(s->sat, clause, nc);
	}
}

desc_s** package_resolve(arch_s* arch){
	dbg_error("");
	desc_s** ret = MANY(desc_s*, DESC_DEFAULT_SIZE);
	solver_s s;
	sat_ctor(&s);

	rbtreeit_s it;
	rbtreeit_ctor(&it, &arch->local->elements, 0);
	desc_s* desc;
	while( (desc=rbtree_iterate_inorder(&it)) ){
		if( desc->reason ){
			dbg_info("~~%s is dependency, skip", desc->name);
			continue;
		}
		if( desc->flags & DESC_FLAG_REMOVED ){
			dbg_info("--%s is removed, skip", desc->name);
			continue;
		}
		if( desc->flags & DESC_FLAG_MAKEPKG ){
			dbg_info("^^%s is makepkg, skip", desc->name);
			continue;
		}
		desc_s* candy = desc_nonvirtual(database_sync_find(arch->sync, desc->name));
		if( !candy ){
			desc_nonvirtual_dump(database_sync_find(arch->sync, desc->name));
			die("internal error, unable to find package %s", desc->name);
		}
		if( candy->flags & DESC_FLAG_CROSS ) {
			dbg_info("##%s is cross, skip", candy->name);
			continue;
		}
		candy->flags |= DESC_FLAG_CROSS;
		dbg_info("++%s->%s", desc->name, candy->name);
		const unsigned var = sat_var(&s, candy);
		c_Lit unit[1];
		unit[0] = sat_lit(var, 0);
		cmsat_add_clause(s.sat, unit, 1);
		dependency_clauses(&s, arch, candy, 1);

       /*     
             Gestione dei conflitti: due pacchetti in conflitto non possono essere entrambi selezionati 
            conflict_t *conf = candidate->conflicts;
            while(conf) {
                int var_conf = get_package_variable(solver, conf->name);
                c_Lit conflict_clause[2];
                conflict_clause[0] = make_lit(var_candidate, 0);  // -var_candidate
                conflict_clause[1] = make_lit(var_conf, 0);         // -var_conf
                cmsat_add_clause(solver, conflict_clause, 2);
                conf = conf->next;
            }
		*/
	}
	dbg_warning("try solve");
   	c_lbool sr = cmsat_solve(s.sat);
	if( sr.x == L_TRUE ){
		slice_lbool model = cmsat_get_model(s.sat);
		unsigned const count = *mem_len(s.var);
		dbg_info("sat successfull, max var %zu, used var %u", model.num_vals, count);
		for (size_t i = 0; i < count; i++) {
			const char *val;
			switch( model.vals[i].x ){
				case L_TRUE : val = "true "; break;
				case L_FALSE: val = "false"; break;
				default     : val = "undef"; break;
			}
			//dbg_info("[%zu] %s %s", i, s.var[i+1]->name, val);
			dbg_info("[%zu] %s", i, val);

		}
	}
	else{
		die("unable to find solution for resolving dependency");
	}
	
	
	rbtreeit_dtor(&it);
	sat_dtor(&s);
	dbg_info("end");
	return ret;
}

/*desc_s** package_cross_dependency(arch_s* arch, desc_s* desc, desc_s** out, unsigned tmp){
	if( !desc->depends ) return out;
	mforeach(desc->depends, i){
		desc_s* dep = database_sync_find(arch->sync, desc->depends[i].name);
		if( !dep ) die("internal error, unable to resolve dependency '%s'", desc->depends[i].name);
		ldforeach(dep, it){
			desc_s* d = it->flags & (DESC_FLAG_PROVIDE | DESC_FLAG_REMOVED) ? it->link : it;
			if( d->flags & DESC_FLAG_CROSS ) continue;
			d->flags |= DESC_FLAG_CROSS;
			unsigned id = mem_ipush(&out);
			out[id] = d;
			dbg_info("%s**%s", ctab(tmp), d->name);
			out = package_cross_dependency(arch, d, out, tmp+1);
		}
	}
	return out;
}

desc_s** package_cross_installed(arch_s* arch){
	dbg_error("");
	desc_s** ret = MANY(desc_s*, DESC_DEFAULT_SIZE);
	
	rbtreeit_s it;
	rbtreeit_ctor(&it, &arch->local->elements, 0);
	desc_s* desc;
	while( (desc=rbtree_iterate_inorder(&it)) ){
		if( desc->reason ){
			dbg_info("~~%s is dependency, skip", desc->name);
			continue;
		}
		if( desc->flags & DESC_FLAG_REMOVED ){
			dbg_info("--%s is removed, skip", desc->name);
			continue;
		}
		if( desc->flags & DESC_FLAG_MAKEPKG ){
			dbg_info("^^%s is makepkg, skip", desc->name);
			continue;
		}
		desc_s* dsync = database_sync_find(arch->sync, desc->name);
		if( !dsync ) die("internal error, unable to find package %s", desc->name);
		ldforeach(dsync, it){
			desc_s* lk;
			if( it->flags & (DESC_FLAG_PROVIDE | DESC_FLAG_REPLACE) ? it->link : it;
			if( lk->flags & DESC_FLAG_CROSS ) continue;
			lk->flags |= DESC_FLAG_CROSS;
			unsigned i = mem_ipush(&ret);
			ret[i] = lk;
			dbg_info("++%s", lk->name);
			ret = package_cross_dependency(arch, lk, ret, 1);
		}
	}
	rbtreeit_dtor(&it);
	dbg_info("end");
	return ret;
}
*/
/*
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cryptominisat_c.h"

// --- Strutture dati per rappresentare un pacchetto, le sue dipendenze e conflitti ---

typedef struct dependency_t {
    char *name;
    unsigned flags;    // Flags per OP_LESS, OP_EQUAL, OP_GREATER, etc.
    char *version;     // Versione richiesta
    struct dependency_t *next;
} dependency_t;

typedef struct conflict_t {
    char *name;
    unsigned flags;
    char *version;
    struct conflict_t *next;
} conflict_t;

typedef struct desc_s {
    char *name;
    char *version;
    struct desc_s *link;      // Se il pacchetto è un PROVIDE, link punta al pacchetto “reale”
    dependency_t *dependencies;
    conflict_t *conflicts;
    int flags;                // Ad es. INSTALLATO, PROVIDE, etc.
    int reason;               // reason==0 indica installazione manuale
} desc_s;

// --- Stub per l’iterazione su strutture ad albero red-black e ricerca nel database sync ---
typedef struct {
} rbtreeit_s;

void rbtreeit_ctor(rbtreeit_s *it, void *elements, int dummy) {
}

desc_s* rbtree_iterate_inorder(rbtreeit_s *it) {
    return NULL;  // placeholder
}

desc_s* database_sync_find(void *sync, const char *name) {
    return NULL;  // placeholder
}

#define ldforeach(head, var) for(desc_s *var = head; var != NULL; var = var->link)

// --- Helper per la creazione e gestione dei literali SAT ---
// In Cryptominisat, un literal è rappresentato in c_Lit come: literal = (var << 1) | sign
// dove sign=0 per il letterale positivo, sign=1 per quello negativo.
c_Lit make_lit(int var, int positive) {
    c_Lit lit;
    lit.x = ((unsigned)var << 1) | (positive ? 0 : 1);
    return lit;
}

// --- Funzione per il controllo della versione ---
// La funzione desc_cmp_version(a, b, flags) restituisce 0 se a->version soddisfa il requisito
// definito in b (in base ai flags), -1 altrimenti.
int desc_cmp_version(desc_s* a, desc_s* b, unsigned flags) {
    return (strcmp(a->version, b->version) == 0) ? 0 : -1;
}

// Helper per verificare la compatibilità di versione di un candidato rispetto a una dependency.
int check_version(desc_s* candidate, dependency_t* dep) {
    desc_s req;
    req.version = dep->version;
    return desc_cmp_version(candidate, &req, dep->flags);
}

// --- Mappatura da nome pacchetto a variabile SAT ---
// Per semplicità, qui si assegna una nuova variabile ad ogni invocazione.
// In una implementazione reale utilizza una hash table per evitare duplicazioni.
int get_package_variable(SATSolver* solver, const char *package_name) {
    static int counter = 0;
    int var = counter++;
     Aggiungiamo una nuova variabile al solver.
       cmsat_new_vars aggiunge 'n' variabili; qui ne aggiungiamo 1 ogni volta. 
    cmsat_new_vars(solver, 1);
    return var;
}

// --- Risoluzione ricorsiva delle dipendenze con controllo delle versioni ---
// Per ogni dependency del pacchetto 'pkg', cerca i candidati nel database sync e
// aggiunge una clausola che impone: se 'pkg' è selezionato, allora almeno uno dei candidati
// compatibili (per versione) deve esserlo.
void add_dependency_clauses(SATSolver* solver, void *sync, desc_s *pkg) {
    int var_pkg = get_package_variable(solver, pkg->name);
    dependency_t *dep = pkg->dependencies;
    while(dep) {
        desc_s* candidates = database_sync_find(sync, dep->name);
        if (!candidates) {
            fprintf(stderr, "Dipendenza %s non trovata per %s\n", dep->name, pkg->name);
        } else {
            c_Lit clause[100];  // Array di dimensione fissa per l'esempio
            int n = 0;
             Aggiungiamo il letterale negativo per pkg: se pkg è selezionato (lit positivo),
               allora deve essere soddisfatta almeno una dependency 
            clause[n++] = make_lit(var_pkg, 0); // negativo di pkg
            int found_candidate = 0;
            ldforeach(candidates, candidate) {
                if (check_version(candidate, dep) == 0) {
                    int var_candidate = get_package_variable(solver, candidate->name);
                    clause[n++] = make_lit(var_candidate, 1); // letterale positivo
                    found_candidate = 1;
                     Propaga ricorsivamente le dipendenze del candidato 
                    add_dependency_clauses(solver, sync, candidate);
                }
            }
            if (!found_candidate) {
                fprintf(stderr, "Nessun candidato compatibile per la dipendenza %s di %s\n", dep->name, pkg->name);
            }
            cmsat_add_clause(solver, clause, n);
        }
        dep = dep->next;
    }
}

int main() {
    SATSolver* solver = cmsat_new();
    if (!solver) {
        fprintf(stderr, "Errore nell'inizializzazione del solver\n");
        return 1;
    }
    
    // Stub per i database: 'local' e 'sync'
    struct {
        void *elements;
    } *local;
    void *sync;
    rbtreeit_s it;
    rbtreeit_ctor(&it, local->elements, 0);
    desc_s *desc;
    while ((desc = rbtree_iterate_inorder(&it)) != NULL) {
        if (desc->reason != 0)
            continue;
        
        desc_s* candidate_list = database_sync_find(sync, desc->name);
        if (!candidate_list) {
            fprintf(stderr, "Pacchetto %s non trovato in sync\n", desc->name);
            continue;
        }
        ldforeach(candidate_list, candidate) {
            int var_candidate = get_package_variable(solver, candidate->name);
             Aggiunge una clausola unitaria che forza l'installazione del pacchetto 
            c_Lit unit_clause[1];
            unit_clause[0] = make_lit(var_candidate, 1);  // letterale positivo
            cmsat_add_clause(solver, unit_clause, 1);
            
             Aggiunge ricorsivamente le clausole per le dipendenze, con controllo versione 
            add_dependency_clauses(solver, sync, candidate);
            
             Gestione dei conflitti: due pacchetti in conflitto non possono essere entrambi selezionati 
            conflict_t *conf = candidate->conflicts;
            while(conf) {
                int var_conf = get_package_variable(solver, conf->name);
                c_Lit conflict_clause[2];
                conflict_clause[0] = make_lit(var_candidate, 0);  // -var_candidate
                conflict_clause[1] = make_lit(var_conf, 0);         // -var_conf
                cmsat_add_clause(solver, conflict_clause, 2);
                conf = conf->next;
            }
        }
    }
    
     Risolvi il problema SAT.
       cmsat_solve restituisce un c_lbool; L_TRUE è definito come 0u. 
    c_lbool ret = cmsat_solve(solver);
    if (ret.x == L_TRUE) {
        slice_lbool model = cmsat_get_model(solver);
        printf("Soluzione SAT trovata (%zu variabili):\n", model.num_vals);
        for (size_t i = 0; i < model.num_vals; i++) {
            const char *val;
            if (model.vals[i].x == L_TRUE)
                val = "True";
            else if (model.vals[i].x == L_FALSE)
                val = "False";
            else
                val = "Undef";
            printf("Variabile %zu: %s\n", i, val);
        }
    } else {
        printf("Nessuna soluzione trovata (o problema UNSAT)!\n");
    }
    
    cmsat_free(solver);
    return 0;
}

*/



































































































































/*
 #include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cryptominisat5/cmsat.h"  // Assicurati del corretto percorso

// --- Strutture dati per il pacchetto ---

typedef struct dependency_t {
    char *name;
    unsigned flags;    // Flags per OP_LESS, OP_EQUAL, OP_GREATER, etc.
    char *version;     // Versione richiesta
    struct dependency_t *next;
} dependency_t;

typedef struct conflict_t {
    char *name;
    unsigned flags;
    char *version;
    struct conflict_t *next;
} conflict_t;

typedef struct desc_s {
    char *name;
    char *version;
    struct desc_s *link;      // Se il pacchetto è un PROVIDE, link punta al pacchetto reale
    dependency_t *dependencies;
    conflict_t *conflicts;
    int flags;                // Ad es. INSTALLATO, PROVIDE, etc.
    int reason;               // reason==0 indica installazione manuale
} desc_s;

// --- Stub e helper per l'iterazione sugli alberi red-black e ricerca nel database ---

typedef struct {
    // struttura interna dell'iteratore
} rbtreeit_s;

void rbtreeit_ctor(rbtreeit_s *it, void *elements, int dummy) {
    // Inizializza l'iteratore sull'albero
}

desc_s* rbtree_iterate_inorder(rbtreeit_s *it) {
    // Restituisce il prossimo elemento in ordine
    return NULL;  // placeholder
}

// Funzione stub per cercare un pacchetto (o la lista di provides) nel database sync
desc_s* database_sync_find(void *sync, const char *name) {
    // Restituisce il pacchetto o la lista di candidate per name
    return NULL;  // placeholder
}

// Macro per iterare su una lista concatenata (per i provides)
#define ldforeach(head, var) for(desc_s *var = head; var != NULL; var = var->link)

// --- Funzione per il controllo della versione ---
// La funzione desc_cmp_version(a, b, flags) restituisce 0 se la versione di 'a' soddisfa il requisito definito in 'b' e dai flag,
// oppure -1 se non compatibile.
int desc_cmp_version(desc_s* a, desc_s* b, unsigned flags) {
    // Stub: implementa la logica reale di comparazione
    if (strcmp(a->version, b->version) == 0)
        return 0;
    else
        return -1;
}

// Helper per confrontare la versione di un candidato con il requisito espresso in una dependency.
// Crea un descrittore temporaneo per il requisito (usando solo il campo version) e usa desc_cmp_version.
int check_version(desc_s* candidate, dependency_t* dep) {
    desc_s req;
    req.version = dep->version;
    return desc_cmp_version(candidate, &req, dep->flags);
}

// --- Mappatura da nome pacchetto a variabile SAT ---
// In una implementazione reale utilizza una hash table e un contatore per assegnare variabili uniche.
int get_package_variable(const char *package_name) {
    static int counter = 1;
    // Se il pacchetto è già mappato, restituisci il valore già assegnato.
    // Altrimenti, assegna un nuovo indice.
    return counter++;
}

// --- Risoluzione ricorsiva delle dipendenze con controllo delle versioni ---
// Per ogni dependency di 'pkg', cerca i candidati nel database sync e aggiungi una clausola
// che impone: se 'pkg' è selezionato allora almeno uno dei candidati compatibili (in versione) deve esserlo.
void add_dependency_clauses(CMSat_solver* solver, void *sync, desc_s *pkg) {
    int var_pkg = get_package_variable(pkg->name);
    dependency_t *dep = pkg->dependencies;
    while(dep) {
        // Cerca nel database sync i candidati per la dependency 'dep->name'
        desc_s* candidates = database_sync_find(sync, dep->name);
        if (!candidates) {
            fprintf(stderr, "Dipendenza %s non trovata per %s\n", dep->name, pkg->name);
            // A questo punto potresti decidere di aggiungere una clausola forzante l'insoddisfacibilità,
            // oppure continuare la risoluzione.
        } else {
            // Costruisci una clausola del tipo:
            // -var_pkg OR (var_candidate1 OR var_candidate2 OR ...)
            // dove includiamo solo i candidati che soddisfano il controllo di versione.
            int clause[100];  // dimensione massima fissa per esempio
            int n = 0;
            clause[n++] = -var_pkg;
            int found_candidate = 0;
            ldforeach(candidates, candidate) {
                if (check_version(candidate, dep) == 0) {
                    int var_candidate = get_package_variable(candidate->name);
                    clause[n++] = var_candidate;
                    found_candidate = 1;
                    // Aggiungi ricorsivamente le dipendenze del candidato
                    add_dependency_clauses(solver, sync, candidate);
                }
            }
            if (!found_candidate) {
                fprintf(stderr, "Nessun candidato compatibile per la dipendenza %s di %s\n", dep->name, pkg->name);
            }
            CMSat_add_clause(solver, clause, n);
        }
        dep = dep->next;
    }
}

int main() {
    // Inizializza il solver di Cryptominisat
    CMSat_solver* solver = CMSat_init();
    if (!solver) {
        fprintf(stderr, "Errore nell'inizializzazione del solver\n");
        return 1;
    }
    
    // Stub per i database: local e sync
    struct {
        void *elements;
    } *local;
    void *sync;
    // ... inizializza local e sync come necessario ...
    
    // Itera sui pacchetti installati manualmente (local)
    rbtreeit_s it;
    rbtreeit_ctor(&it, local->elements, 0);
    desc_s *desc;
    while ((desc = rbtree_iterate_inorder(&it)) != NULL) {
        // Considera solo i pacchetti installati manualmente (reason == 0)
        if (desc->reason != 0)
            continue;
        
        // Trova il pacchetto (o la lista di provides) nel database sync
        desc_s* candidate_list = database_sync_find(sync, desc->name);
        if (!candidate_list) {
            fprintf(stderr, "Pacchetto %s non trovato in sync\n", desc->name);
            continue;
        }
        ldforeach(candidate_list, candidate) {
            int var_candidate = get_package_variable(candidate->name);
            // Impone il pacchetto installato come unit clause
            CMSat_add_clause(solver, &var_candidate, 1);
            // Aggiungi le clausole per risolvere le dipendenze ricorsivamente, con controllo versione
            add_dependency_clauses(solver, sync, candidate);
            
            // Gestisci i conflitti: se due pacchetti sono in conflitto, non possono essere entrambi selezionati.
            conflict_t *conf = candidate->conflicts;
            while(conf) {
                int var_conf = get_package_variable(conf->name);
                int clause[2];
                clause[0] = -var_candidate;
                clause[1] = -var_conf;
                CMSat_add_clause(solver, clause, 2);
                conf = conf->next;
            }
        }
    }
    
    // Risolvi il problema SAT
    lbool ret = CMSat_solve(solver);
    if (ret == l_True) {
        const int* model = CMSat_get_model(solver);
        int num_vars = CMSat_get_nvars(solver);
        printf("Soluzione SAT trovata:\n");
        for (int i = 0; i < num_vars; i++) {
            printf("Variabile %d: %s\n", i, (model[i] == l_True ? "True" : "False"));
        }
    } else {
        printf("Nessuna soluzione trovata!\n");
    }
    
    CMSat_destroy(solver);
    return 0;
}
*/

/*
 
Certamente! Gestire pacchetti che forniscono versioni specifiche di una funzionalità (provides) aggiunge complessità alla risoluzione delle dipendenze. Per affrontare questo scenario utilizzando CryptoMiniSat in C, è necessario rappresentare le versioni delle funzionalità come variabili distinte e definire clausole che esprimano le relazioni tra pacchetti e versioni.

Scenario Esteso:

Pacchetto A: dipende dalla funzionalità X versione 1.
Pacchetto B: fornisce la funzionalità X versione 1.
Pacchetto C: fornisce la funzionalità X versione 2.
Pacchetto D: dipende dalla funzionalità X versione 2 e non può coesistere con il pacchetto C.
Obiettivo: Determinare una configurazione valida di installazione dei pacchetti che soddisfi le dipendenze, rispetti i conflitti e consideri le versioni specifiche delle funzionalità fornite.

Implementazione in C:

c
Copia
Modifica
#include <stdio.h>
#include "cryptominisat_c.h"  // Assicurati di includere l'header corretto per l'interfaccia C

int main(void) {
    // Inizializza il solver
    CMSat_Solver* solver = CMSat_init();

    // Definisci le variabili per i pacchetti e le versioni delle funzionalità
    // Assegnazione delle variabili:
    // 1: Pacchetto A
    // 2: Pacchetto B
    // 3: Pacchetto C
    // 4: Pacchetto D
    // 5: Funzionalità X v1
    // 6: Funzionalità X v2

    CMSat_new_vars(solver, 6);

    // Clausola 1: Se A è installato, allora X v1 deve essere disponibile (A → X v1)
    // Rappresentato come: ¬A ∨ X v1
    int clause1[] = {-1, 5, 0};
    CMSat_add_clause(solver, clause1);

    // Clausola 2: Se B è installato, allora fornisce X v1 (B → X v1)
    // Rappresentato come: ¬B ∨ X v1
    int clause2[] = {-2, 5, 0};
    CMSat_add_clause(solver, clause2);

    // Clausola 3: Se C è installato, allora fornisce X v2 (C → X v2)
    // Rappresentato come: ¬C ∨ X v2
    int clause3[] = {-3, 6, 0};
    CMSat_add_clause(solver, clause3);

    // Clausola 4: Se D è installato, allora X v2 deve essere disponibile (D → X v2)
    // Rappresentato come: ¬D ∨ X v2
    int clause4[] = {-4, 6, 0};
    CMSat_add_clause(solver, clause4);

    // Clausola 5: D e C non possono essere installati insieme (¬(D ∧ C))
    // Rappresentato come: ¬D ∨ ¬C
    int clause5[] = {-4, -3, 0};
    CMSat_add_clause(solver, clause5);

    // Clausola 6: X v1 e X v2 non possono essere entrambe disponibili simultaneamente
    // Rappresentato come: ¬X v1 ∨ ¬X v2
    int clause6[] = {-5, -6, 0};
    CMSat_add_clause(solver, clause6);

    // Clausola 7: X v1 o X v2 devono essere disponibili
    // Rappresentato come: X v1 ∨ X v2
    int clause7[] = {5, 6, 0};
    CMSat_add_clause(solver, clause7);

    // Risolvi il problema SAT
    CMSat_lbool result = CMSat_solve(solver);

    if (result == CMSAT_TRUE) {
        printf("Soluzione trovata:\n");
        for (int i = 1; i <= 4; i++) {
            CMSat_lbool val = CMSat_get_model(solver, i);
            printf("Pacchetto %c: %s\n", 'A' + i - 1, (val == CMSAT_TRUE) ? "installato" : "non installato");
        }
        for (int i = 5; i <= 6; i++) {
            CMSat_lbool val = CMSat_get_model(solver, i);
            printf("Funzionalità X v%d: %s\n", i - 4, (val == CMSAT_TRUE) ? "disponibile" : "non disponibile");
        }
    } else if (result == CMSAT_FALSE) {
        printf("Nessuna soluzione soddisfacente trovata.\n");
    } else {
        printf("Soluzione indeterminata.\n");
    }

    // Libera le risorse del solver
    CMSat_free(solver);

    return 0;
}
Spiegazione delle Clausole Aggiuntive:

Clausola 6: Le versioni X v1 e X v2 della funzionalità non possono essere disponibili contemporaneamente, rappresentato come ¬X v1 ∨ ¬X v2.
Clausola 7: Almeno una versione della funzionalità X deve essere disponibile, rappresentato come X v1 ∨ X v2.
Note:

Le variabili 5 e 6 rappresentano rispettivamente le versioni 1 e 2 della funzionalità X.
Le clausole assicurano che le dipendenze di versione siano rispettate e che non ci siano conflitti tra versioni diverse della stessa funzionalità.
Questo approccio consente di modellare scenari in cui pacchetti forniscono versioni specifiche di una funzionalità e altri pacchetti dipendono da versioni particolari. Utilizzando un solver SAT come CryptoMiniSat, è possibile determinare una c
*/



























//I PACCHETTI SONO IN ZST unzstd
//
/*
-rw-r--r-- root/root      9393 2024-10-15 16:18 .BUILDINFO
-rw-r--r-- root/root      4098 2024-10-15 16:18 .MTREE
-rw-r--r-- root/root      1064 2024-10-15 16:18 .PKGINFO
drwxr-xr-x root/root         0 2024-10-15 16:18 usr/
drwxr-xr-x root/root         0 2024-10-15 16:18 usr/bin/
lrwxrwxrwx root/root         0 2024-10-15 16:18 usr/bin/rview -> vim
lrwxrwxrwx root/root         0 2024-10-15 16:18 usr/bin/rvim -> vim
-rwxr-xr-x root/root   4821944 2024-10-15 16:18 usr/bin/vim
lrwxrwxrwx root/root         0 2024-10-15 16:18 usr/bin/vimdiff -> vim
-rwxr-xr-x root/root      2154 2024-10-15 16:18 usr/bin/vimtutor
-rwxr-xr-x root/root     22704 2024-10-15 16:18 usr/bin/xxd
drwxr-xr-x root/root         0 2024-10-15 16:18 usr/share/
drwxr-xr-x root/root         0 2024-10-15 16:18 usr/share/applications/
-rw-r--r-- root/root      5604 2024-10-15 16:18 usr/share/applications/vim.desktop
drwxr-xr-x root/root         0 2024-10-15 16:18 usr/share/icons/
drwxr-xr-x root/root         0 2024-10-15 16:18 usr/share/icons/hicolor/
drwxr-xr-x root/root         0 2024-10-15 16:18 usr/share/icons/hicolor/48x48/
drwxr-xr-x root/root         0 2024-10-15 16:18 usr/share/icons/hicolor/48x48/apps/
-rw-r--r-- root/root       474 2024-10-15 16:18 usr/share/icons/hicolor/48x48/apps/gvim.png
drwxr-xr-x root/root         0 2024-10-15 16:18 usr/share/icons/locolor/
drwxr-xr-x root/root         0 2024-10-15 16:18 usr/share/icons/locolor/16x16/
*/

char* test_perm(unsigned perm, char ftype){
	__private char out[512];
	const char *rwx = "rwx";
	unsigned i = 0;
	out[i++] = ftype;
	for (; i < 9; i++) {
		unsigned bit = (perm >> (8 - i)) & 1;
		out[i] = bit ? rwx[i % 3] : '-';
	}
    if (perm & 04000) out[2] = (out[2] == 'x') ? 's' : 'S';
    if (perm & 02000) out[5] = (out[5] == 'x') ? 's' : 'S';
    if (perm & 01000) out[8] = (out[8] == 'x') ? 't' : 'T';
	out[i] = 0;
	return out;
}

char* test_user(unsigned uid, unsigned gid){
	__private char out[512];
	if( uid == 0 && gid == 0 ){
		return "root:root";
	}
	sprintf(out, "%u:%u", uid, gid);
	return out;
}





/*
int pp_package_sync(ppConfig_s* conf, ppProgress_s* prog, const char* src, const char* dst){
	__free void* zstdtar  = load_file(src, 1);
	dbg_info("decompress %s", src);
	__free void* pkgtar = zstd_decompress(zstdtar);
	//FILE* f = fopen("./ciao.tar", "w");
	//fwrite(pkgtar, 1, mem_header(pkgtar)->len, f);
	//fclose(f);

	__tar tar_s tar;
	dbg_info("open tar");
	tar_mopen(&tar, pkgtar);
	tarent_s* ent;
	dbg_info("mkdir %s", dst);
	mk_dir(dst, 0644);
	while( (ent=tar_next(&tar)) ){
		if( ent->type == TAR_FILE ){
			dbg_info("%s %s %s", test_perm(ent->perm, '-'), test_user(ent->uid,ent->gid), ent->path);
			mem_free(ent);
		}
		else if( ent->type == TAR_DIR ){
			dbg_info("%s %s %s", test_perm(ent->perm, 'd'), test_user(ent->uid,ent->gid), ent->path);
			mem_free(ent);
		}
		else if( ent->type == TAR_SYMBOLIC_LINK ){
			dbg_info("%s %s %s -> %s", test_perm(ent->perm, 'l'), test_user(ent->uid,ent->gid), ent->path, (char*)ent->data);
			mem_free(ent);
		}
		else{
			die("unaspected tar type: %u", ent->type);
		}
	}
	if( (errno=tar_errno(&tar)) ){
		die("unable to unpack database");
	}
	//extract
	//get new desc
	//install
	return 0;
}


void pp_test(ppConfig_s* conf){
	__free char* src = str_printf("%s/vim-9.1.0785-1-x86_64.pkg.tar.zst", conf->options.cacheDir);
	__free char* dst = str_printf("%s/vim-9.1.0785-1-x86_64", conf->options.cacheDir);
	dbg_info("src: %s", src);
	dbg_info("dst: %s", dst);
	pp_package_sync(conf, NULL, src, dst);
}
*/

#include <gcc-plugin.h>
#include <tree-pass.h>
#include <context.h>
#include <tree.h>
#include <basic-block.h>
#include <gimple.h>
#include <gimple-iterator.h>
#include <graph.h>
#include <graphviz.h>

#include <c-family/c-pragma.h>
#include <diagnostic-core.h>
#include <vec.h>

#include <vector>
#include <set>
#include <algorithm>


// ---------- GLOBALES ---------- //

/**
 * Plugin name
 */
#define PLUGIN_NAME "mpi_collective_functions_pass"

/**
 * Plugin version
 */
#define PLUGIN_VERSION "1.0"

/**
 * Required GCC version
 */
#define PLUGIN_GCC_BASEVERSION "12.2.0"

/**
 * récupération du code des fonctions collectives MPI
 */
enum mpi_collective_code {
#define DEFMPICOLLECTIVES( CODE, NAME ) CODE,
#include "MPI_collectives.def"
    LAST_AND_UNUSED_MPI_COLLECTIVE_CODE
#undef DEFMPICOLLECTIVES
};

/** 
 * récupération du nom des fonctions collectives MPI
 */
#define DEFMPICOLLECTIVES( CODE, NAME ) NAME,
const char *const mpi_collective_name[] = {
#include "MPI_collectives.def"
};
#undef DEFMPICOLLECTIVES

int plugin_is_GPL_compatible;


static vec<tree> fname_vec;
vec<const unsigned char*> pragma_instrumented_functions {};

void insert_pragma(tree elem){
    fname_vec.safe_push(elem);
}

void dump_fname_vec(void){
    for (auto elem : fname_vec) {
        printf("%s, ", IDENTIFIER_POINTER(elem));
    }
    printf("\n");
}

static void handle_pragma_fx(cpp_reader *dummy ATTRIBUTE_UNUSED){
    enum cpp_ttype token;
    bool close_paren_needed = false;
    tree pragma_arg;

    if (cfun) {
        // pragma ProjetCA ne doit pas être dans une fonction
        error("%<#pragma instrument option%> pragma not allowed inside a function definition");
        return;
    }

    token = pragma_lex(&pragma_arg);
    if (CPP_OPEN_PAREN == token) {
        close_paren_needed = true;
        token = pragma_lex(&pragma_arg);
        if (CPP_NAME != token) {
            error("%<#pragma instrumente option%> is not a name");
            return;
        }

        insert_pragma(pragma_arg);
        dump_fname_vec();

        token = pragma_lex(&pragma_arg);
        while (CPP_COMMA == token) {
            token = pragma_lex(&pragma_arg);
            if (CPP_COMMA != token) {
                if (CPP_NAME != token) {
                    error("%<#pragma instrumente option%> is not a name");
                    return;
                }
                insert_pragma(pragma_arg);
                dump_fname_vec();
            }
            token = pragma_lex(&pragma_arg);
        }
    } else if (CPP_NAME == token) {
        insert_pragma(pragma_arg);
        dump_fname_vec();
        token = pragma_lex(&pragma_arg);
    }

    if (CPP_CLOSE_PAREN == token) {
        if (!close_paren_needed) {
            error("%<#pragma instrumente option%> unexpected closing perenthesis");
        }
        close_paren_needed = false;
        token = pragma_lex(&pragma_arg);
    }

    if (CPP_EOF == token) {
        if (close_paren_needed) {
            error("%<#pragma instrumente option%> missing closing perenthesis");
        }
    }
}



// ---------- GRAPHVIZ ---------- //

char* cfgviz_generate_filename(function *fun, const char* suffix){
	char* target_filename;
	target_filename = (char*)xmalloc(2048*sizeof(char));
	snprintf(target_filename, 1024, "%s_%s_%s_%s.dot",
		current_function_name(),
		LOCATION_FILE(fun->function_start_locus),
		LOCATION_FILE(fun->function_start_locus),
		suffix);
	return target_filename;
}

void cfgviz_internal_dump(function *fun, FILE *out){
	fprintf(out, "Digraph G{\n");
	
	basic_block bb;
	FOR_ALL_BB_FN(bb, fun){
		int index = bb->index;
		edge e;
		edge_iterator ei;

		fprintf(out, "N%d [label=\"Node %d\" shape=ellipse]\n", index, index);
		FOR_EACH_EDGE(e, ei, bb->succs){
			basic_block src = e->src;
			basic_block dst = e->dest;
			fprintf(out, "N%d -> N%d [color=red label=\"\"]\n", src->index, dst->index);
		}
	}
	fprintf(out, "}\n");
}


void cfgviz_dump(function *fun, const char* suffix){
	char* target_filename;
	FILE *out;
	target_filename = cfgviz_generate_filename(fun, suffix);
	printf("[GARPHVIZ] Generating CFG of function %s in file <%s>\n",
		current_function_name(), target_filename);
	out = fopen(target_filename, "w");
	cfgviz_internal_dump(fun,out);
	fclose(out);
	free(target_filename);
}


// ---------- UTILS ---------- //

//retourne le code caractérisant la fonction collective mpi, si la fonction passée en paramètre en est une 
enum mpi_collective_code get_mpi_func_code(gimple * stmt){
	//si l'instruction n'est pas un appel de fonction, elle ne peut pas appeler de fonction MPI
	if(!is_gimple_call(stmt)) return LAST_AND_UNUSED_MPI_COLLECTIVE_CODE;

	//on récupère le nom de la fonction appelée par l'instruction
	const char* func_name = IDENTIFIER_POINTER(DECL_NAME(gimple_call_fndecl(stmt)));

	//si le nom de la fonction appelée par l'instruction est une des fonctions collectives MPI, on retourne son code
	for(int i = 0; i < LAST_AND_UNUSED_MPI_COLLECTIVE_CODE; i++){
		if(strcmp(func_name, mpi_collective_name[i]) == 0) {
			printf("MPI function DETECTED : %s", func_name);
			return mpi_collective_code(i);
		}
	}
	
	return LAST_AND_UNUSED_MPI_COLLECTIVE_CODE;
} 

enum mpi_collective_code get_mpi_func_code(basic_block * bb){
    mpi_collective_code code;

    for (gimple_stmt_iterator gsi = gsi_start_bb((*bb)); !gsi_end_p(gsi); gsi_next(&gsi)) {
        gimple* stmt = gsi_stmt(gsi);
        code = get_mpi_func_code(stmt);
        if(code != LAST_AND_UNUSED_MPI_COLLECTIVE_CODE) return code;
    }

    return LAST_AND_UNUSED_MPI_COLLECTIVE_CODE;
}


//retourne le nombre d'appels à une fonction mpi contient la fonction envoyée en paramètre
int get_nb_mpi_calls_in(basic_block * bb){
    int nb_mpi_calls = 0;

    FOR_EACH_BB_FN(bb, cfun){
		for (gimple_stmt_iterator gsi = gsi_start_bb((*bb)); !gsi_end_p(gsi); gsi_next(&gsi)) {
            gimple* stmt = gsi_stmt(gsi);
            if(get_mpi_func_code(stmt) != LAST_AND_UNUSED_MPI_COLLECTIVE_CODE) nb_mpi_calls++;
        }
	}
}

//divise chaque bloc de la fonction en 2 blocs pour n'avoir au plus qu'une fonction collective mpi par bloc
void divide_blocks(function * fun){
	basic_block bb;

	FOR_EACH_BB_FN(bb, fun){
		//repère de la dernière fonction mpi trouvée dans le bloc
		gimple* last_mpi_stmt = NULL;
		for (gimple_stmt_iterator gsi = gsi_start_bb(bb); !gsi_end_p(gsi); gsi_next(&gsi)) {
            gimple* stmt = gsi_stmt(gsi);
			//si appel de fonction mpi trouvé
            if(get_mpi_func_code(stmt) != LAST_AND_UNUSED_MPI_COLLECTIVE_CODE){
				//si ce n'est pas la première fonction mpi que l'on rencontre, on divise le bloc
				if(last_mpi_stmt != NULL) split_block(bb, last_mpi_stmt);
				//on change le point de repère par la dernière fonction mpi trouvée
				last_mpi_stmt = stmt;
			}
        }
	}
}


void get_bbs_for_each_rank(function *fun) {
    std::vector<std::set<basic_block>> bbs_for_each_rank;

    basic_block entry_bb = ENTRY_BLOCK_PTR_FOR_FN(fun);
    basic_block exit_bb = EXIT_BLOCK_PTR_FOR_FN(fun);

    bbs_for_each_rank.push_back({entry_bb});

    while (std::find(bbs_for_each_rank.back().begin(), bbs_for_each_rank.back().end(), exit_bb) == bbs_for_each_rank.back().end()) {
        std::set<basic_block> next_rank_set;

        // non fonctionnel, remplacer la boucle for par un while non vide sur une pile
        for (basic_block bb : bbs_for_each_rank.back()) {
            if (bb->succs) {
                for (size_t i = 0; i < bb->succs->length(); ++i) {
                    edge e = (*bb->succs)[i];
                    if (e->dest) {
                        //si appel de fonction mpi trouvé
                        if (get_mpi_func_code(stmt) == LAST_AND_UNUSED_MPI_COLLECTIVE_CODE) {
                            bbs_for_each_rank.back().insert(e->dest);
                        }
                        else {
                            next_rank_set.insert(e->dest);  // Ensures unique blocks for each rank
                        }
                    }
                }
            }
        }
    }

    // Convert sets to vectors for the final output
    std::vector<std::vector<basic_block>> bb_ranked;
    for (const auto& rank_set : bbs_for_each_rank) {
        bb_ranked.push_back(std::vector<basic_block>(rank_set.begin(), rank_set.end()));
    }
}


//vérifie s'il y a un problème de concordance dans les rangs du cfg pour les fonctions mpi présentes
bool is_mpi_rank_correct(vector<vector<basic_block>> bb_ranked){
	//pour chaque rang
	for(int i = 0; i < bb_ranked.size; i++){
		if(!bb_ranked[i].empty){
            mpi_collective_code rank_code = NULL;
            //on vérifie qu'il y a au moins un basic_block dans le rang actuel
            if(!bb_ranked[i][0].empty)
                //pour chaque basic_block au rang i
                for(int j = 0; j < bb_ranked[i].size; j++){
                    if(get_nb_mpi_calls_in(bb_ranked[i][j]) == 1){
                        enum mpi_collective_code curr_rank_code = get_mpi_func_code(&bb_ranked[i][j]);
                        if(rank_code == NULL){
                            rank_code = curr_rank_code;
                        }else{
                            if(rank_code != curr_rank_code){
                                printf("Incohérence dans l'ordre des appels de fonction MPI au rang %d\n", i);
                                return false;
                            }
                        }
                    }
                }
		    }
	    }

	return true;
}

// ---------- GCC PLUGIN DEFINITION ---------- //


/**
 * 
 */
const pass_data mpi_pass_data {
    GIMPLE_PASS,
    PLUGIN_NAME,
    OPTGROUP_NONE,
    TV_NONE,
    PROP_gimple_any,
    0,
    0,
    0,
    0,
};

/**
 * 
 */
class mpi_collective_pass : public gimple_opt_pass {
    public:
        mpi_collective_pass(gcc::context *ctxt) : 
            gimple_opt_pass (mpi_pass_data, ctxt){}
        
        mpi_collective_pass* clone(){
            return new mpi_collective_pass(g);
        }

        bool gate(function *fun){
            printf("Ceci est une porte : \n");

            const char* func_name = fndecl_name(cfun->decl);
            printf("Examen de la fonction : %s\n", func_name);

            return true;
		};

        unsigned int execute(function *fun){
	    
		cfgviz_dump(fun, "before_split");

	    const char* func_name = fndecl_name(cfun->decl);
	    printf("Plugin is executing verification on function %s.... \n", func_name);
	    
        //on divise les blocks pour chaque nouvel appel de fonction mpi après le premier trouvé
	    divide_blocks(fun);

	    cfgviz_dump(fun, "after_split");


        vector<vector<basic_block>> bb_ranked = get_bbs_for_each_rank(fun);
        is_mpi_rank_correct(bb_ranked);


        //initialisation des bitmaps
        basic_block bb;
	    bitmap_head *cfg_nodes;
	    cfg_nodes = XNEWVEC (bitmap_head, last_basic_block_for_fn (fun));

	    FOR_ALL_BB_FN(bb, fun) {
            bitmap_initialize(&cfg_nodes[bb->index], &bitmap_default_obstack);
        }
    
        return 0;
        }
};

// ---------- GCC PLUGIN INITIALIZATION ---------- //

/**
 * Initialisation du plugin
 */
int plugin_init(struct plugin_name_args *plugin_infos, struct plugin_gcc_version *plugin_version){
    printf("Initialisation de mon plugin...");

    c_register_pragma("instrumente", "function", handle_pragma_fx);
    printf("Registered new pragmas\n");
    
    mpi_collective_pass mpi_pass(g);

    struct register_pass_info mpi_pass_info;

    mpi_pass_info.pass = &mpi_pass;
    mpi_pass_info.reference_pass_name = "cfg";
    mpi_pass_info.ref_pass_instance_number = 0;
    mpi_pass_info.pos_op = PASS_POS_INSERT_AFTER;

    register_callback(PLUGIN_NAME, PLUGIN_PASS_MANAGER_SETUP, NULL, &mpi_pass_info);
    return 0;
}

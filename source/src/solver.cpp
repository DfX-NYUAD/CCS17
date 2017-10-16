#include "solver.h"
#include "util.h"
#include "sim.h"
#include "sld.h"

#include <iterator>
#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <fstream> 

solver_t::solver_t(ckt_n::ckt_t& c, ckt_n::ckt_t& s, int verb)
    : ckt(c)
    , simckt(s)
    , sim(s, s.ckt_inputs)
    , dbl(c, ckt_n::dup_allkeys, true)
    , input_values(ckt.num_ckt_inputs(), false)
    , output_values(ckt.num_outputs(), false)
    , fixed_keys(ckt.num_key_inputs(), false)
    , verbose(verb)
    , iter(0)
    , backbones_count(0)
    , cube_count(0)
{
    MAX_VERIF_ITER = 1;
    time_limit = 1e100;

    using namespace ckt_n;
    using namespace sat_n;
    using namespace AllSAT;

    assert(dbl.dbl->num_outputs() == 1);
    assert(dbl.ckt.num_ckt_inputs() == ckt.num_ckt_inputs());

    // dbl.dbl->split_gates();
    // dbl.dbl->dump(std::cout);

    dbl.dbl->init_solver(S, cl, lmap, true);
    node_t* out = dbl.dbl->outputs[0];
    l_out = lmap[out->get_index()];
    cl.verbose = verbose;

    if(verbose) {
        std::cout << dbl.ckt << std::endl;
        std::cout << *dbl.dbl << std::endl;
        std::cout << "DBL MAPPINGS" << std::endl;
        dump_mappings(std::cout, *dbl.dbl, lmap);
    }

    // setup arrays of literals.
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        int idx = ckt.key_inputs[i]->get_index();
        keyinput_literals_A.push_back(lmap[dbl.pair_map[idx].first->get_index()]);
        keyinput_literals_B.push_back(lmap[dbl.pair_map[idx].second->get_index()]);
    }
    for(unsigned i=0; i != ckt.num_ckt_inputs(); i++) {
        int idx = ckt.ckt_inputs[i]->get_index();
        cktinput_literals.push_back(lmap[dbl.pair_map[idx].first->get_index()]);
    }
    for(unsigned i=0; i != ckt.num_outputs(); i++) {
        int idx = ckt.outputs[i]->get_index();
        node_t* outA = dbl.pair_map[idx].first;
        node_t* outB = dbl.pair_map[idx].second;
        Lit lA = lmap[outA->get_index()];
        Lit lB = lmap[outB->get_index()];
        output_literals_A.push_back(lA);
        output_literals_B.push_back(lB);
    }

    S.freeze(keyinput_literals_A);
    S.freeze(keyinput_literals_B);
    S.freeze(cktinput_literals);
    S.freeze(output_literals_A);
    S.freeze(output_literals_B);
    S.freeze(l_out);

    dbl_keyinput_flags.resize(S.nVars(), false);
    dbl.dbl->init_keyinput_map(lmap, dbl_keyinput_flags);
}


void solver_t::addKnownKeys(std::vector<std::pair<int, int> >& values)
{
    for(unsigned i=0; i != values.size(); i++) {
        using namespace sat_n;
        Lit lA = keyinput_literals_A[values[i].first];
        Lit lB = keyinput_literals_B[values[i].first];
        assert(values[i].second == 0 || values[i].second == 1);
        if(values[i].second) {
            S.addClause(lA);
            S.addClause(lB);
        } else {
            S.addClause(~lA);
            S.addClause(~lB);
        }
    }
}

solver_t::~solver_t()
{
}

bool solver_t::solve(solver_t::solver_version_t ver, std::map<std::string, int>& keysFound, bool quiet)
{
    assert( SOLVER_V0 == ver );
    return _solve_v0(keysFound, quiet, -1);
}

void solver_t::_record_sim(
    const std::vector<bool>& input_values, 
    const std::vector<bool>& output_values, 
    std::vector<sat_n::lbool>& values
)
{
    using namespace sat_n;
    using namespace ckt_n;
    using namespace AllSAT;

    iovectors.push_back(iovalue_t());
    int last = iovectors.size()-1;
    iovectors[last].inputs = input_values;
    iovectors[last].outputs = output_values;

    // extract inputs and put them in the array.
    for(unsigned i=0; i != input_values.size(); i++) {
        lbool val = input_values[i] ? l_True : l_False;
        int jdx  = dbl.dbl->ckt_inputs[i]->get_index();
        assert(var(lmap[jdx]) < values.size());
        assert(var(lmap[jdx]) >= 0);
        values[var(lmap[jdx])] = val;

    }

    // and then the outputs.
    for(unsigned i=0; i != ckt.num_outputs(); i++) {
        node_t* n_i = ckt.outputs[i];
        int idx = n_i->get_index();
        int idxA = dbl.pair_map[idx].first->get_index();
        int idxB = dbl.pair_map[idx].second->get_index();
        Var vA = var(lmap[idxA]);
        Var vB = var(lmap[idxB]);
        assert(vA < values.size() && vA >= 0);
        assert(vB < values.size() && vB >= 0);
        if(output_values[i] == true) {
            values[vA] = values[vB] = sat_n::l_True;
        } else {
            values[vA] = values[vB] = sat_n::l_False;
        }
    }
}

// Evaluates the output for the values stored in input_values and then records
// this in the solver.
void solver_t::_record_input_values()
{
    using namespace sat_n;
    using namespace ckt_n;
    using namespace AllSAT;
    std::vector<sat_n::lbool> values(S.nVars(), sat_n::l_Undef);

    _queryOracle () ; //query DfX oracle 
    //sim.eval(input_values, output_values);
    _record_sim(input_values, output_values, values);
    int cnt = cl.addRewrittenClauses(values, dbl_keyinput_flags, S);
    __sync_fetch_and_add(&cube_count, cnt);
}

bool solver_t::_solve_v0(std::map<std::string, int>& keysFound, bool quiet, int dlimFactor)
{
    using namespace sat_n;
    using namespace ckt_n;
    using namespace AllSAT;

    // add all zeros.
    /*for(unsigned i=0; i != dbl.dbl->num_ckt_inputs(); i++) { 
        input_values[i]=false; 
    }
    _record_input_values();

    // and all ones.
    for(unsigned i=0; i != dbl.dbl->num_ckt_inputs(); i++) { 
        input_values[i]=true; 
    }
    _record_input_values();
*/
    bool done = false;
    while(true) {
        bool result = S.solve(l_out);
        if(dlimFactor != -1) {
            int dlim = dlimFactor * S.nVars();
            if(dlim <= S.getNumDecisions()) {
                std::cout << "too many decisions! giving up." << std::endl;
                break;
            }
        }

        __sync_fetch_and_add(&iter, 1);
        //std::cout << "iteration #" << iter << std::endl;
        //std::cout << "vars: " << S.nVars() << "; clauses: " << S.nClauses() << std::endl;
        // std::string filename = "solver" + boost::lexical_cast<std::string>(iter) + ".cnf";
        //S.writeCNF(filename);

        /*if(verbose) {
            dbl.dump_solver_state(std::cout, S, lmap);
            std::cout << std::endl;
        }*/
        std::cout << "iteration: " << iter 
                  << "; vars: " << S.nVars() 
                  << "; clauses: " << S.nClauses() 
                  << "; decisions: " << S.getNumDecisions() << std::endl;

        if(false == result) {
            done = true;
            break;
        }

        // now extract the inputs.
        for(unsigned i=0; i != dbl.dbl->num_ckt_inputs(); i++) {
            int jdx  = dbl.dbl->ckt_inputs[i]->get_index();
            lbool val = S.modelValue(lmap[jdx]);
            assert(val.isDef());
            if(!val.getBool()) {
                input_values[i] = false;
            } else {
                //input_values[i] = false; // just for test 
                input_values[i] = true;
            }
        }
        _record_input_values();
        //if(verbose) {
            std::cout << "input: " << input_values 
                << "; output: " << output_values << std::endl;
        //}

        // _sanity_check_model();

        struct rusage ru_current;
        getrusage(RUSAGE_SELF, &ru_current);
        if(utimediff(&ru_current, &ru_start) > time_limit) {
            std::cout << "timeout in the slice loop." << std::endl;
            break;
        }
    }
    if(done) {
        std::cout << "finished solver loop." << std::endl;
        _verify_solution_sim(keysFound);
    }
    return done;
#if 0
    _verify_solution_sat();
#endif
}

void solver_t::_sanity_check_model()
{
    using namespace sat_n;
    using namespace ckt_n;

    bool pass = true;
    vec_lit_t assumps;
    std::vector<bool> actual_output_values;

    for(unsigned i=0; i != cktinput_literals.size(); i++) {
        bool vi = input_values[i];
        assumps.push( vi ? cktinput_literals[i] : ~cktinput_literals[i]);
    }
    if(verbose) dump_clause(std::cout << "assumps: ", assumps) << std::endl;
    if(S.solve(assumps) == false) {
        std::cout << "UNSAT result during sanity check." << std::endl;
        std::cout << "result of no-assumption solve: " << S.solve() << std::endl;
        exit(1);
    }
    if(verbose) {
        std::cout << "[expected] input: " << input_values 
            << "; output: " << output_values << std::endl;
    }

    if(verbose) {
        dump_clause(std::cout << "sat input: ", assumps) << std::endl;
        std::cout << "sat output: ";
    }
    for(unsigned i=0; i != output_values.size(); i++) {
        bool vi = output_values[i];
        lbool ri = S.modelValue(output_literals_A[i]);
        if(verbose) {
            std::cout << (ri.isUndef() ? "-" : (ri.getBool() ? "1" : "0"));
        }
        if(!(ri.isDef() && ri.getBool() == vi)) { 
            pass = false;
        }
    }
    if(verbose) std::cout << std::endl;

    if(pass) {
        if(verbose) {
            std::cout << "simulation sanity check passed." << std::endl;
        }
    } else {
        std::cout << "simulation sanity check failed." << std::endl;
        exit(1);
    }
}

bool solver_t::_verify_solution_sim(std::map<std::string, int>& keysFound)
{
    using namespace sat_n;
    using namespace ckt_n;

    srand(142857142);
    bool pass = true;
    for(int iter=0; iter < MAX_VERIF_ITER;  iter++) {
        vec_lit_t assumps;
        std::vector<bool> input_values;
        std::vector<bool> output_values;

        for(unsigned i=0; i != cktinput_literals.size(); i++) {
            bool vi = bool(rand() % 2);
            assumps.push( vi ? cktinput_literals[i] : ~cktinput_literals[i]);
            input_values.push_back(vi);
        }
	
   	//std::cout << input_values << "input_values"  << std::endl;  
	_queryOracle() ; // query DfX oracle  
        //sim.eval(input_values, output_values);
        if(verbose) {
            std::cout << "input: " << input_values 
                      << "; output: " << output_values << std::endl;
            dump_clause(std::cout << "assumps: ", assumps) << std::endl;
        }

        if(S.solve(assumps) == false) {
            std::cout << "UNSAT model!" << std::endl;
            return false;
        }

        if(verbose) {
            dump_clause(std::cout << "sat input: ", assumps) << std::endl;
            std::cout << "sat output: ";
        }
        for(unsigned i=0; i != output_values.size(); i++) {
            bool vi = output_values[i];
            lbool ri = S.modelValue(output_literals_A[i]);
            if(verbose) {
                std::cout << (ri.isUndef() ? "-" : (ri.getBool() ? "1" : "0"));
            }
            if(!(ri.isDef() && ri.getBool() == vi)) { 
                pass = false;
            }
        }
        if(iter == 0) {
            for(unsigned i=0; i != keyinput_literals_A.size(); i++) {
                lbool v = S.modelValue(keyinput_literals_A[i]);
                if(!v.getBool()) {
                    keysFound[ckt.key_inputs[i]->name] = 0;
                } else {
                    keysFound[ckt.key_inputs[i]->name] = 1;
                }
            }
        }
        if(verbose) std::cout << std::endl;
        if(!pass) {
            if(verbose) {
                dbl.dump_solver_state(std::cout, S, lmap);
                std::cout << std::endl;
            }
            std::cout << "sim failed." << std::endl;
            break;
        }
    }
    return pass;
}

bool solver_t::_verify_solution_sat()
{
    using namespace sat_n;
    vec_lit_t c1, c2;

    assert(keyinput_literals_A.size() == keyinput_literals_B.size());
    c1.growTo(keyinput_literals_A.size()+1);
    c2.growTo(keyinput_literals_B.size()+1);

    for(unsigned i=0; i != keyinput_literals_A.size(); i++) {
        c1[i+1] = ~keyinput_literals_A[i];
        c2[i+1] = ~keyinput_literals_B[i];
    }
    c1[0] = l_out;
    c2[0] = l_out;
    if(S.solve(c1) == false && S.solve(c2) == false) {
        return true;
    } else {
        return true;
    }
}

void solver_t::findFixedKeys(std::map<int, int>& backbones)
{
    using namespace ckt_n;
    using namespace sat_n;

    if(iovectors.size() == 0) return;

    Solver Sckt;
    AllSAT::ClauseList cktCl;
    index2lit_map_t cktmap;
    std::vector<bool> keyinputflags;

    ckt.init_solver(Sckt, cktCl, cktmap, true /* don't care. */);
    keyinputflags.resize(Sckt.nVars(), false);
    ckt.init_keyinput_map(cktmap, keyinputflags);

    std::vector<lbool> values(Sckt.nVars(), sat_n::l_Undef);

    for(unsigned i=0; i != iovectors.size(); i++) {
        const std::vector<bool>& inputs = iovectors[i].inputs;
        const std::vector<bool>& outputs = iovectors[i].outputs;

        for(unsigned i=0; i != inputs.size(); i++) {
            int idx = ckt.ckt_inputs[i]->get_index();
            values[var(cktmap[idx])] = inputs[i] ? sat_n::l_True : sat_n::l_False;
        }

        for(unsigned i=0; i != outputs.size(); i++) {
            int idx = ckt.outputs[i]->get_index();
            values[var(cktmap[idx])] = outputs[i] ? sat_n::l_True : sat_n::l_False;
        }
        cktCl.addRewrittenClauses(values, keyinputflags, Sckt);
    }
    // now freeze the ckt inputs.
    for(unsigned i=0; i != ckt.num_ckt_inputs(); i++) {
        Lit li = cktmap[ckt.ckt_inputs[i]->get_index()];
        Sckt.freeze(li);
    }
    // and then freeze the key inputs.
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        Lit li = cktmap[ckt.key_inputs[i]->get_index()];
        Sckt.freeze(li);
    }

    // get an assignment for the keys.
    std::cout << "finding initial assignment of keys." << std::endl;
    bool result = Sckt.solve();
    assert(result);
    std::vector<Lit> keys;
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        int idx = ckt.key_inputs[i]->get_index();
        lbool value = Sckt.modelValue(var(lmap[idx]));
        keys.push_back(value == sat_n::l_True ? lmap[idx] : ~lmap[idx]);
    }
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        //std::cout << "key: " << i << std::endl;
        if(Sckt.solve(~keys[i]) == false) {
            // can't satisfy these I/O combinations with this key value.
            if(sign(keys[i])) {
                backbones[i] = 0;
            } else {
                backbones[i] = 1;
            }
            Sckt.addClause(keys[i]);
        }
    }

#if 0
    for(unsigned i=0; i != iovectors.size(); i++) {
        //std::cout << "iovector: " << i << std::endl;

        const std::vector<bool>& inputs = iovectors[i].inputs;
        _testBackbones(inputs, Sckt, cktmap, backbones);
    }
#endif
    //std::cout << "# of backbones found: " << backbones.size() << std::endl;
}
// function queryOcarcle added for quering the oracle named DfX 
void solver_t::_queryOracle() { 

    std::string cmd;
    for(unsigned i=0; i != dbl.dbl->num_ckt_inputs(); i++) { 
         if (input_values[i]==false) 
	{
		cmd+= "0 "; 
	}
	else{
		cmd+= "1 ";	
	}
    } 
    // FIXME: printing input_values here leads to an error somehow 	   
    //std::cout << input_values << "input_values"  << std::endl;  
    //std::cout << "./DfX "<< cmd << " cmd"  << std::endl;  

    cmd =  "./DfX " + cmd + " > output.txt"; // "../test/a.out " 
    int status = std::system (cmd.c_str()); 	
    if (status < 0)
           std::cout << "Error with code " << strerror(errno) << ", while quering Oracle" << std::endl;	
    // later to be a function 
    std::ifstream infile ("output.txt")  ;
    std::string line; 
    std::string line1; 


    //getline (infile, line) ; 		
    getline (infile, line) ; 		
    /*for (int i = 0; i < line.size(); i++) {
    	line1+= line[i];  
    	line1+= " " ; 
    }*/
    
    //std::cout << line << " Line Read: "<< std::endl; 	
    std::vector<std::string> strs;
    boost::split(strs, line , boost::is_any_of(" "));
   //std::cout << "size of strs: "<< strs.size() << std::endl; 
   int ocnt = 0;  
   for (std::vector<std::string>::iterator it = strs.begin(); it!= strs.end() ; ++it ){
	
	if (*it == "0")	{
		output_values[ocnt] = false;	
		//std::cout << "got 0" << std::endl; 
	}
	else{
		output_values[ocnt] = true;	
		//std::cout << "got 1" << std::endl; 
	}
	ocnt++; 
    }

} // end of function queryOracle 


void solver_t::_testBackbones(
    const std::vector<bool>& inputs, 
    sat_n::Solver& S, ckt_n::index2lit_map_t& lmap,
    std::map<int, int>& backbones)
{
#if 0
    using namespace sat_n;
    using namespace ckt_n;

    assert(inputs.size() == ckt.num_ckt_inputs());

    vec_lit_t assumps;
    for(unsigned i=0; i != inputs.size(); i++) {
        int idx = ckt.ckt_inputs[i]->get_index();
        assumps.push(inputs[i] ? lmap[idx] : ~lmap[idx]);
    }

    assert(assumps.size() == ckt.num_ckt_inputs());
    assumps.growTo(ckt.num_ckt_inputs() + 1);
    assert(assumps.size() == ckt.num_ckt_inputs()+1);

    int last = assumps.size()-1;
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        assumps[last] = ~keys[i];
        //std::cout << "key: " << i << std::endl;
        if(S.solve(assumps) == false) {
            // can't satisfy these I/O combinations with this key value.
            if(sign(keys[i])) {
                backbones[i] = 0;
            } else {
                backbones[i] = 1;
            }
            S.addClause(keys[i]);
        }
    }
#endif
}

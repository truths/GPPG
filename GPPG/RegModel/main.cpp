#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace std;

#include <Base/FitnessFunction.h>

#include "Operation/GreedyLoad.h"
#include <Operation/GreedyLoadMap.h>
#include <Model/Sequence/Operation.h>
#include <Model/Sequence/IO.h>

#include <Base/Simulator.h>
#include <Operation/OperationHeap.h>
#include <Operation/BaseCompressionPolicy.h>
#include <Operation/UbiOperationGraph.h>
#include <Simulator/EvoSimulator.h>

#include <Model/Pathway/Operation.h>
#include <Model/Pathway/IO.h>
#include <Model/Pathway/Fitness.h>
#include <Util/json/json.h>

#define SUPPORTS_RUSAGE

#ifdef SUPPORTS_RUSAGE
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

using namespace GPPG;
using namespace GPPG::Model;
using namespace GPPG::Model::TransReg;


inline double timeToDbl(const timeval& t) { return t.tv_sec + 1.0*t.tv_usec/1e6; }

void recordUsage(ostream* out, int step, int generation) {
	rusage stats;
	getrusage( RUSAGE_SELF, &stats );
	(*out) << step << "," << generation << "," << (clock()*1.0/CLOCKS_PER_SEC) << ","
			<< timeToDbl(stats.ru_utime) << "," << timeToDbl(stats.ru_stime) << ","
			<< stats.ru_maxrss << "," << stats.ru_ixrss << "," << stats.ru_idrss << "," << stats.ru_isrss << ","
			<< stats.ru_minflt << "," << stats.ru_majflt << ","
			<< stats.ru_nswap << ","
			<< stats.ru_inblock << "," << stats.ru_oublock << ","
			<< stats.ru_msgsnd << "," << stats.ru_msgrcv << ","
			<< stats.ru_nsignals << ","
			<< stats.ru_nvcsw << "," << stats.ru_nivcsw << endl;
	out->flush();
}

// http://linux.die.net/man/2/getrusage

void runSimulation( EvoSimulator* sim, long N, long G, int steps, ostream* out ) {
	cout << "Running Simulation [N="<<N<<", G="<<G<<"]\n";

#ifdef SUPPORTS_RUSAGE
	if (out) {
		(*out) << "step,gen,wtime,utime,stime,maxrss,ixrss,idrss,isrss,minflt,majflt,nswap,inblock,oublock,msgsnd,msgrcv,nsignals,nvcsw,nivcsw\n";
	}
#endif
	for (int i=0; i<steps; i++) {
		sim->evolve( N, G/steps );	
		cout << "Done with " << i << " of " << steps << endl;
#ifdef SUPPORTS_RUSAGE
		if (out) {
			recordUsage( out, i, sim->clock() );
		}
#endif
	}
	
}

void outputGenotypes( EvoSimulator* sim, ostream& out ) {	
		
	set<IGenotype*>::iterator git;
	const set<IGenotype*>& active = sim->activeGenotypes();
	int i=0;
	for (git=active.begin(); git!=active.end(); git++) {
		IGenotype* g = *git;
		out << ">g"<<i<<"|"<<g->key() << "|" <<g->frequency()<<"|"<<g->order()<<endl;
		out << g->exportFormat();
		out << endl << endl;
		i++;
	}
	
}

void outputOperations( EvoSimulator* sim, ostream& out ) {	
		
	set<IOperation*>::iterator git;
	OperationGraph* graph = (OperationGraph*)sim->heap();
	const set<IOperation*>& ops = graph->operations();
	int i=0;
	out << "GenotypeOutId,Generation,Type,Cost,IsCompressed,IsActive,Frequency,Parent1,Parent2,NumChildren,Data\n";
	for (git=ops.begin(); git!=ops.end(); git++) {
		const IOperation* op = *git;
		out << op->key() << "," << op->order() << "," << typeid(*op).name() << "," << op->cost() << "," << op->isCompressed() << "," << op->isActive() << "," <<
			op->frequency() << ",";
		if( op->numParents() > 0 ) out << op->parent(0)->key();
		out << ",";
		if( op->numParents() > 1) out << op->parent(1)->key();
		out << "," << op->numChildren() << ",\"" << op->toString() << "\"" << endl;
		i++;
	}
	
}

GlobalInfo* readGenomeData(const string& filename) {
	ifstream t( filename.c_str() );
	stringstream buffer;
	buffer << t.rdbuf();
	Json::Value root;
	Json::Reader reader;
	bool parsingSuccessful = reader.parse(buffer.str(), root);
	if (!parsingSuccessful) {
		cout << "Failed to parse genome file\n" << reader.getFormatedErrorMessages();
		return 0;
	}
	
	cout << root << endl;
	
	// Read Motifs
	Json::Value cmos = root["motifs"];
	vector<string> motifNames = cmos.getMemberNames(); // list of motif names
	map<string, string> motifSeq; // Maps motif name -> sequence
	for(int i=0; i<motifNames.size(); i++) {
		motifSeq[ motifNames[i] ] = cmos[motifNames[i]].asString();
	}
	
	// Read Genes
	Json::Value genes = root["genes"];
	std::vector<string> geneNames; // list of gene names
	std::vector<int> regions; // list of region length, same length as genes
	std::map<int, std::vector<int> > binding; // relates binding motif -> tf 
	std::vector<int> tfs; // list of gene id's which are TFs
	std::string curr_motif = "";
	Json::Value gene_motifs;
	int motif_index;
	for(int i=0; i<genes.size(); i++) {
		geneNames.push_back(genes[i]["name"].asString());
		regions.push_back(genes[i]["upstream"].asInt());
		if( genes[i].isMember("binding_motifs") && genes[i]["binding_motifs"].size() > 0 ) {
			gene_motifs = genes[i]["binding_motifs"];
			tfs.push_back(i); // Store gene index
			for(int j=0; j<gene_motifs.size(); j++) {
				curr_motif = gene_motifs[j].asString();
				if( motifSeq.count( curr_motif) ) {
					motif_index = find(motifNames.begin(), motifNames.end(), curr_motif ) - motifNames.begin();
					binding[motif_index].push_back( i ) ;
				}
			}
		}
	}
	
	// Create Global Info
	GlobalInfo* info = new GlobalInfo(geneNames, regions, motifNames, tfs, binding, motifSeq);
	
	return info;
}

double IUPACtoGainRate(const string& motif) {
	for(int i=0; i<motif.length(); i++) {
		
	}
	return 1e-5;
}

double IUPACtoLossRate(const string& motif) {
	return 1e-5;
}

EvoSimulator* createSimulator( const Json::Value& config ) {
	double scaling = config.get("scaling",1).asDouble();
	
	// Set Compression
	ICompressionPolicy* policy=0;
	const Json::Value& compression = config["compression"];
	const string& compName = compression.get("name","Store-Root").asString();
	
	if( compName == "Greedy-Load" ) policy = new GreedyLoad(compression.get("k",20).asInt(), compression.get("t",10).asInt());
	else if( compName == "Store-Root" ) policy = new BaseCompressionPolicy(STORE_ROOT); 
	else if( compName == "Store-Active" ) policy = new BaseCompressionPolicy(STORE_ACTIVE);
	else if( compName == "Store-All" ) policy = new BaseCompressionPolicy(STORE_ALL);
	
	if(!policy) {
		cout << "No valid compression policy provided, got: " << compName << endl;
		return 0;
	}
	
	EvoSimulator* sim = new EvoSimulator( new OperationGraph( policy ) );
	
	// Get Global Info from genome data
	GlobalInfo* info = readGenomeData( config["genome"].asString() );
	if(!info) {
		cout << "Unable to read genome data" << endl;
		return 0;
	}
	
	cout << *info << endl;
	
	// Use info to generate binding site mutator
	double u = config["sequence_mutation"].asDouble();
	int bs_size = config["binding_site_size"].asInt();
	std::vector<double> gainRates, lossRates;
	for(int i=0; i<info->numMotifs(); i++) {
		gainRates.push_back( scaling*IUPACtoGainRate(info->getMotifSequence(info->getMotifName(i)) )); // TODO: Do we need to add scaling here?
		lossRates.push_back( IUPACtoLossRate(info->getMotifSequence(info->getMotifName(i)) ));
	}
	
	sim->addMutator( new BindingSiteMutator( config.get("cost",1).asDouble(), u*scaling, (bs_size-1)/2, gainRates, lossRates));
											
	// Add viability constraint
	// TODO: Add viability constraint
	IFitnessFunction* func = new ConnectedFitness();
	sim->setFitnessFunction( func );
	
	// Use info to generate random network
	IGenotype* g;
	PathwayRootFactory factory(*info);
	g = factory.random();
	sim->addGenotype( g, 1.0 );
	cout << "Fitness: " << func->calculate(g) << endl;
	
	// Return the simulator!
	return sim;
}

void createAndRunSimulation( const Json::Value& config ) {
	
	EvoSimulator* sim = createSimulator( config );
	
	if (!sim) {
		cout << "Failed to create simulator\n";
		return;
	}

	const Json::Value& output = config["output"];
	ofstream* perfFile = 0;
	if( output.isMember("performance") ) {
		perfFile = new ofstream();
		perfFile->open( output["performance"].asCString() );
	}

	runSimulation(sim, config["individuals"].asInt(), config["generations"].asInt(), config.get("steps", 100).asInt(), perfFile);
	
	/*
	if(perfFile) {
		perfFile->close();
		delete perfFile;
	}
	
	if( output.isMember("individuals") ) {
		if( output["individuals"] == "<stdout>") {
			outputGenotypes( sim, cout );
		}
		else {
			ofstream out(output["individuals"].asCString());
			outputGenotypes( sim, out);
			out.close();
		}
	}
	
	if( output.isMember("operations") ) {
		ofstream out(output["operations"].asCString());
		outputOperations( sim, out);
		out.close();
	}
	*/
	
	delete sim;
}

int main (int argc, char * const argv[])
{
	// First argument is config file.
	if( argc < 2 ) {
		cout << "Please provide a config file\n";
		return -1;
	}
	
	cout << "Reading configuration file " << argv[1] << endl;
	ifstream t( argv[1] );
	stringstream buffer;
	buffer << t.rdbuf();
	
	Json::Value root;
	Json::Reader reader;
	bool parsingSuccessful = reader.parse(buffer.str(), root);
	if (!parsingSuccessful) {
		cout << "Failed to parse configuration\n" << reader.getFormatedErrorMessages();
		return -1;
	}
	cout << root << endl;
	
	createAndRunSimulation(root);
	
	return 0;
}


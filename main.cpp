//
//  main.cpp
//  NB_C++
//
//  Created by Alexandru Cristian on 06/04/2017.
//
//

#include "NB.hpp"
#include "Diskutil.hpp"
#include <cstring>
#include <cstdlib>
#include <boost/program_options.hpp>
using namespace std;
namespace p_opt = boost::program_options;

string usageMsg("./NB.run [mode: train/classify/benchmark] [source dir] [options]\n");
const int CONF_THRESHOLD = 100000;
const int DEF_KMER_SIZE = 6;
const string GEN_SEQ_EXTEN(".fasta");
const string KMER_EXTEN(".kmr");
const string PROG_VER("NB v. 0.1.5a-dev.");
const string DEF_SAVEDIR("./NB_save");

void trainNB(NB &nb, path srcdir, string extension, int nbatch,
             int memoryLimit){
  int count = 1, counter = 0, usedMemory = 0;
  vector<tuple<string, path, path> > result =
    Diskutil::getTrainingGenomePaths(srcdir, extension);
  string cls_s="-1"; Class<int> *current = NULL;
  for(vector<tuple<string, path, path> >::iterator iter = result.begin();
      iter != result.end(); iter++, counter++){
        if(cls_s.compare(get<0>(*iter)) != 0){

          cls_s = get<0>(*iter);

          current = nb.getClass(cls_s);

          if(current == NULL){
            path save_file = path(nb.getSavedir().native()
                                  + path::preferred_separator
                                  + cls_s + "-save.dat");
            current = new Class<int>(cls_s,
                                     nb.getKmerSize(),
                                     save_file);
            nb.addClass(current);
          }
          nb.addClassToUpdateQueue(current);
        }

        // FASTA files not needed for training, so just add up the kmr file size
        int genomeSize = Diskutil::getFileSize(get<1>(*iter));

        if(memoryLimit != -1 && usedMemory + genomeSize > memoryLimit){
          nb.processClassUpdates();
          usedMemory = 0;
        }

        Genome *genome = new Genome(get<1>(*iter), get<2>(*iter));
        current->queueGenome(genome);
        usedMemory += genomeSize;

        if(nbatch != -1 && counter % nbatch == 0){
          nb.processClassUpdates();
        }
      }

      nb.processClassUpdates();
}

int printClassifierResults(vector<Genome*> reads,
                           vector<tuple<string, path, path> > result){

  int correct=0, total = reads.size();

  for(int i=0; i < total; i++){
    Genome::pqueue queue = reads[i]->getConfidences();
    string pred_class = queue.top().second->getId();

    cout<<"Genome with class "<<get<0>(result[i]);
    cout<<", predicted "<<pred_class<<", score "<<queue.top().first<<'\n';
    cout.flush();

    if(pred_class.compare(get<0>(result[i])) == 0){
      correct++;
    }

    int position=1;
    while(!queue.empty() && pred_class.compare(get<0>(result[i])) != 0){
      queue.pop(); position++;
      pred_class = queue.top().second->getId();
    }

    if(pred_class.compare(get<0>(result[i])) != 0){
      cout<<"[ERROR] Actual class not in queue.\n";
    }else{
      cout<<"Actual class was on position "<<position<<", score "<<queue.top().first<<"\n";
    }
  }

  for(vector<Genome*>::iterator iter = reads.begin();
      iter != reads.end(); iter++){
        delete *iter;
  }

  return correct;
}

void classifyNB(NB &nb, path srcdir, string extension, int nbatch,
                int memoryLimit){
  vector<tuple<string, path, path> > result =
    Diskutil::getTrainingGenomePaths(srcdir, extension);
  vector<Genome*> reads;
  int correct=0, counter=0, usedMemory = 0, total = result.size();
  for(vector<tuple<string, path, path> >::iterator iter =
    result.begin(); iter != result.end(); iter++, counter++){

    int genomeSize = Diskutil::getFileSize(get<1>(*iter));
    genomeSize += Diskutil::getFileSize(get<2>(*iter));

    if(memoryLimit != -1 && usedMemory + genomeSize > memoryLimit){
      nb.classify(reads);
      correct += printClassifierResults(reads, result);
      usedMemory = 0;

      iter = result.erase(result.begin(), result.begin() + reads.size());
      reads.clear();
    }

    if(nbatch != -1 && counter != 0 && counter % nbatch == 0){
      nb.classify(reads);
      correct += printClassifierResults(reads, result);
      usedMemory = 0;

      iter = result.erase(result.begin(), result.begin() + reads.size());
      reads.clear();
    }

    Genome *genome = new Genome(get<1>(*iter), get<2>(*iter));
    reads.push_back(genome);
    usedMemory += genomeSize;
  }

  nb.classify(reads);
  correct += printClassifierResults(reads, result);
  usedMemory = 0;

  cout<<"Accuracy: "<<correct*1.0/total<<"\n";
  cout<<"Total correct: "<<correct<<"\n";
  cout<<"Total classified: "<<total<<"\n";
  cout.flush();
}

int main(int argc, char* argv[]){
  int nbatch, nthreads, kmersize, memLimit;
  string kmer_ext, srcdir, mode, savedir;

  p_opt::options_description generic("Generic options");
  generic.add_options()
    ("help,h", "Print help message")
    ("version,v", "Print version information");

  p_opt::options_description hidden("Hidden options");
  hidden.add_options()
    ("mode", "Sets mode of program, train or classify.")
    ("srcdir", "Path to source folder");

  p_opt::options_description visible("Allowed options");
  visible.add_options()
    ("savedir,s", p_opt::value<string>(&savedir)->default_value(DEF_SAVEDIR),
                  "Path to save folder")
    ("kmersize,k", p_opt::value<int>(&kmersize)->default_value(DEF_KMER_SIZE),
                   "Kmer size used in count files")
    ("memlimit,m", p_opt::value<int>(&memLimit)->default_value(-1),
                   "Cap memory use to a predefined value (KBs).")
    ("nthreads,t", p_opt::value<int>(&nthreads)->default_value(1),
                 "Number of threads to spawn, 1 by default")
    ("ext,e", p_opt::value<string>(&kmer_ext)->default_value(KMER_EXTEN),
            "Extension of kmer count files, \".kmr\" by default")
    ("nbatch,n", p_opt::value<int>(&nbatch)->default_value(-1),
               "Number of genomes to load at one time in memory, \
all at once by default")
  ;

  p_opt::positional_options_description pos_args;
  pos_args.add("mode", 1);
  pos_args.add("srcdir", 1);

  p_opt::options_description cmdline_options;
  cmdline_options.add(generic).add(visible).add(hidden);

  p_opt::variables_map opt_map;
  p_opt::store(
    p_opt::command_line_parser(argc, argv).options(cmdline_options)
                                          .positional(pos_args).run(),
    opt_map);
  p_opt::notify(opt_map);

  if(opt_map.count("version")){
    cout<<PROG_VER<<"\n";
    return 1;
  }

  if(opt_map.count("help") || opt_map.count("mode") == 0
     || opt_map.count("srcdir") == 0){
    cout<<usageMsg<<"\n"<<generic<<"\n"<<visible<<"\n";
    return 1;
  }

  srcdir = opt_map["srcdir"].as<string>();
  mode = opt_map["mode"].as<string>();

  create_directories(savedir);
  NB nb(kmersize, path(savedir), nthreads);
  nb.debug_flag = NB::Debug::LOG_SOME;

  if(mode.compare("train") == 0){
    cout<<"Train mode.\n";

    trainNB(nb, path(srcdir), kmer_ext, nbatch, memLimit);

    cout<<"Training complete.\n";

  }else if(mode.compare("classify") == 0){
    cout<<"Classify mode.\n";

    classifyNB(nb, path(srcdir), kmer_ext, nbatch, memLimit);

  }else if(mode.compare("benchmark") == 0){
    trainNB(nb, path(srcdir), kmer_ext, nbatch, memLimit);

    classifyNB(nb, path(srcdir+"_test"), kmer_ext, nbatch, memLimit);
  }else{
    cout<<usageMsg<<"\n"<<generic<<"\n"<<visible<<"\n";
    return 1;
  }
  return 0;
}
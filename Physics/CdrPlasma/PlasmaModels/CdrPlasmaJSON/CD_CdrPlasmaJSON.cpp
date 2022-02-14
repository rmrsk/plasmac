/* chombo-discharge
 * Copyright © 2022 SINTEF Energy Research.
 * Copyright © 2022 NTNU.
 * Copyright © 2022 Fanny Skirbekk. 
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @file   CD_CdrPlasmaJSON.cpp
  @brief  Implementation of CD_CdrPlasmaJSON.H
  @author Robert Marskar, Fanny Skirbekk
*/

// Std includees
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

// Chombo includes
#include <ParmParse.H>
#include <CH_Timer.H>

// Our includes
#include <CD_CdrPlasmaJSON.H>
#include <CD_DataParser.H>
#include <CD_Random.H>
#include <CD_Units.H>
#include <CD_NamespaceHeader.H>

using namespace Physics::CdrPlasma;

CdrPlasmaJSON::CdrPlasmaJSON(){
  CH_TIME("CdrPlasmaJSON::CdrPlasmaJSON()");

  // Parse options and read JSON file.
  this->parseOptions();
  this->parseJSON();

  // Parse initial data.
  this->initializeNeutralSpecies();
  this->initializePlasmaSpecies();
  this->initializePhotonSpecies();  
  this->initializeSigma();

  // Do a sanity check of the species. 
  this->sanityCheckSpecies();

  // Parse the Townsend ionization and attachment coefficients.
  this->parseAlpha();
  this->parseEta();

  // Parse CDR mobilities and diffusion coefficients. 
  this->parseMobilities();
  this->parseDiffusion();
  this->parseTemperatures();

  // Parse plasma-reactions and photo-reactions
  this->parsePlasmaReactions();
  this->parsePhotoReactions();

  // Parse secondary emission on electrodes and dielectrics
  this->parseElectrodeReactions();
  this->parseDielectricReactions();
  this->parseDomainReactions();

  m_numCdrSpecies = m_cdrSpecies.size();
  m_numRtSpecies  = m_rtSpecies. size();
}

CdrPlasmaJSON::~CdrPlasmaJSON(){
  CH_TIME("CdrPlasmaJSON::~CdrPlasmaJSON()");
}

void CdrPlasmaJSON::parseOptions() {
  CH_TIME("CdrPlasmaJSON::parseOptions");
  
  ParmParse pp("CdrPlasmaJSON");

  pp.get("verbose",          m_verbose );
  pp.get("chemistry_file",   m_jsonFile);
  pp.get("discrete_photons", m_discretePhotons);
  pp.get("skip_reactions",   m_skipReactions);
}

void CdrPlasmaJSON::parseJSON() {
  CH_TIME("CdrPlasmaJSON::parseJSON()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseJSON()" << endl;
  }

  if(!(this->doesFileExist(m_jsonFile))) this->throwParserError("CdrPlasmaJSON::parseJSON -- file '" + m_jsonFile + "' does not exist");

  // Parse the JSON file
  std::ifstream istream(m_jsonFile);  
  istream >> m_json;
}

void CdrPlasmaJSON::throwParserError(const std::string a_error) const {
  pout() << a_error << endl;

  MayDay::Error(a_error.c_str());
}

void CdrPlasmaJSON::throwParserWarning(const std::string a_warning) const {
  pout() << a_warning << endl;

  MayDay::Warning(a_warning.c_str());
}

bool CdrPlasmaJSON::containsWildcard(const std::string a_str) const {
  return (a_str.find("@") != std::string::npos);
}

void CdrPlasmaJSON::sanityCheckSpecies() const {
  CH_TIME("CdrPlasmaJSON::sanityCheckSpecies()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::sanityCheckSpecies()" << endl;
  }

  std::string baseError = "CdrPlasmaJSON::sanityCheckSpecies";

  std::vector<std::string> allSpecies;

  for (const auto& s : m_neutralSpeciesMap) {
    allSpecies.emplace_back(s.first);
  }

  for (const auto& s : m_cdrSpeciesMap) {
    allSpecies.emplace_back(s.first);
  }

  for (const auto& s : m_rteSpeciesMap) {
    allSpecies.emplace_back(s.first);
  }

  const int numSpecies = allSpecies.size();

  // Sort the container. 
  std::sort(allSpecies.begin(), allSpecies.end());
  for (int i = 0; i < allSpecies.size() - 1; i++){
    if(allSpecies[i] == allSpecies[i+1]){
      this->throwParserError(baseError + " -- species '" + allSpecies[i] + "' was defined more than once. Double-check your neutral, plasma, and photon species");
    }
  }
}

std::string CdrPlasmaJSON::trim(const std::string& a_string) const {
  auto ltrim = [](const std::string a_s) -> std::string {
    std::string s = a_s;
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
				    std::not1(std::ptr_fun<int, int>(std::isspace))));
    return s;
  };

  auto rtrim = [](std::string a_s) -> std::string {
    std::string s = a_s;    
    s.erase(std::find_if(s.rbegin(), s.rend(),
			 std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
    return s;
  };

  return ltrim(rtrim(a_string));
}

void CdrPlasmaJSON::parseReactionString(std::vector<std::string>& a_reactants,
					std::vector<std::string>& a_products,
					const std::string&        a_reaction) const {
  CH_TIME("CdrPlasmaJSON::parseReactionString()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseReactionString()" << endl;
  }

  const std::string baseError = "CdrPlasmaJSON::parseReactionString";

  // Parse the string into segments. We split on white-space. 
  std::stringstream ss(a_reaction);
  std::string segment;
  std::vector<std::string> segments;
    
  while(std::getline(ss, segment, ' ')){
    segments.push_back(segment);
  }

  // Discard all whitespace and solitary + from input string
  segments.erase(std::remove(segments.begin(), segments.end(), "" ), segments.end());
  segments.erase(std::remove(segments.begin(), segments.end(), "+"), segments.end());

  // Find the element containing "->"
  const auto& it = std::find(segments.begin(), segments.end(), "->");  

  // Make sure that -> is in the reaction string.
  if(it == segments.end()) this->throwParserError(baseError + " -- Reaction '" + a_reaction + "' does not contain '->");

  // Left of "->" are reactants and right of "->" are products
  a_reactants = std::vector<std::string>(segments.begin(), it);
  a_products  = std::vector<std::string>(it + 1, segments.end());
}

void CdrPlasmaJSON::getReactionSpecies(std::list<int>&                 a_plasmaReactants,
				       std::list<int>&                 a_neutralReactants,
				       std::list<int>&                 a_photonReactants,					    
				       std::list<int>&                 a_plasmaProducts,
				       std::list<int>&                 a_neutralProducts,
				       std::list<int>&                 a_photonProducts,				       
				       const std::vector<std::string>& a_reactants,
				       const std::vector<std::string>& a_products) const {
  CH_TIME("CdrPlasmaJSON::getReactionSpecies()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::getReactionSpecies()" << endl;
  }

  const std::string baseError = "CdrPlasmaJSON::getReactionSpecies ";

  a_plasmaReactants. clear();
  a_neutralReactants.clear();
  a_photonReactants. clear();
  
  a_plasmaProducts.  clear();
  a_neutralProducts. clear();
  a_photonProducts.  clear();

  // Figure out the reactants. 
  for (const auto& r : a_reactants){

    // Figure out if the reactants is a plasma species or a neutral species.
    const bool isPlasma  = this->isPlasmaSpecies (r);
    const bool isNeutral = this->isNeutralSpecies(r);
    const bool isPhoton  = this->isPhotonSpecies (r);

    if(isPlasma && !(isNeutral || isPhoton)){
      a_plasmaReactants.emplace_back(m_cdrSpeciesMap.at(r));
    }
    else if(isNeutral && !(isPlasma || isPhoton)){
      a_neutralReactants.emplace_back(m_neutralSpeciesMap.at(r));
    }
    else if(isPhoton && !(isPlasma || isNeutral)){
      a_photonReactants.emplace_back(m_rteSpeciesMap.at(r));
    }
    else{
      this->throwParserError(baseError + "-- logic bust 1");
    }
  }

  // Figure out the reactants. 
  for (const auto& p : a_products){

    // Figure out if the reactants is a plasma species or a neutral species.
    const bool isPlasma  = this->isPlasmaSpecies (p);
    const bool isNeutral = this->isNeutralSpecies(p);
    const bool isPhoton  = this->isPhotonSpecies (p);

    if(isPlasma && !(isNeutral || isPhoton)){
      a_plasmaProducts.emplace_back(m_cdrSpeciesMap.at(p));
    }
    else if(isNeutral && !(isPlasma || isPhoton)){
      a_neutralProducts.emplace_back(m_neutralSpeciesMap.at(p));
    }
    else if(isPhoton && !(isPlasma || isNeutral)){
      a_photonProducts.emplace_back(m_rteSpeciesMap.at(p));
    }
    else{
      this->throwParserError(baseError + "-- logic bust 2");
    }
  }  
}

void CdrPlasmaJSON::initializeNeutralSpecies() {
  CH_TIME("CdrPlasmaJSON::initializeNeutralSpecies()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::initializeNeutralSpecies()" << endl;
  }

  const std::string baseError = "CdrPlasmaJSON::initializeNeutralSpecies";

  // These fields are ALWAYS required
  if(!(m_json["gas"].contains("law"            ))) this->throwParserError(baseError + " but field 'law' is missing"            );
  if(!(m_json["gas"].contains("neutral species"))) this->throwParserError(baseError + " but field 'neutral species' is missing");

  // JSON entry
  const auto gasJSON = m_json["gas"];  

  // Get the gas law
  const auto gasLaw = trim(gasJSON["law"].get<std::string>());

  // For converting grams to kilograms
  constexpr Real g2kg = 1.E-3;  

  // Instantiate the pressure, density, and temperature of the gas. Note: The density  is the NUMBER density. 
  if(gasLaw == "ideal"){

    // These fields are required
    if(!(gasJSON.contains("temperature"))) this->throwParserError(baseError + " and got ideal gas law but field 'temperature' is missing");
    if(!(gasJSON.contains("pressure"   ))) this->throwParserError(baseError + " and got ideal gas law but field 'pressure' is missing");
    
    // Set the gas temperature, density, and pressure from the ideal gas law. No extra parameters needed and no variation in space either.
    const Real T0   = gasJSON["temperature"].get<Real>() ;
    const Real P0   = gasJSON["pressure"   ].get<Real>() ;
    const Real P    = P0 * Units::atm2pascal;
    const Real Rho0 = (P * Units::Na) / (T0 * Units::R);

    m_gasTemperature = [T0  ] (const RealVect a_position) -> Real { return T0;   };
    m_gasPressure    = [P   ] (const RealVect a_position) -> Real { return P;    };
    m_gasDensity     = [Rho0] (const RealVect a_position) -> Real { return Rho0; };
  }
  else if(gasLaw == "troposphere"){

    // These fields are required.
    if(!(gasJSON.contains("temperature"))) this->throwParserError(baseError + " and got troposphere gas law but field 'temperature' is missing");
    if(!(gasJSON.contains("pressure"   ))) this->throwParserError(baseError + " and got troposphere gas law but field 'pressure' is missing");    
    if(!(gasJSON.contains("molar mass" ))) this->throwParserError(baseError + " and got troposphere gas law but field 'molar mass' is missing");    
    if(!(gasJSON.contains("gravity"    ))) this->throwParserError(baseError + " and got troposphere gas law but field 'gravity' is missing");    
    if(!(gasJSON.contains("lapse rate" ))) this->throwParserError(baseError + " and got troposphere gas law but field 'lapse rate' is missing");

    const Real T0   = gasJSON["temperature"].get<Real>();
    const Real P0   = gasJSON["pressure"   ].get<Real>();
    const Real g    = gasJSON["gravity"    ].get<Real>();
    const Real L    = gasJSON["lapse rate" ].get<Real>();
    const Real M    = gasJSON["molar mass" ].get<Real>();
    const Real gMRL = (g * g2kg * M) / (Units::R * L);
    const Real P    = P0 * Units::atm2pascal;

    // Temperature is T = T0 - L*(z-h0)
    m_gasTemperature = [T0, L] (const RealVect a_position) -> Real {
      return T0 - L * a_position[SpaceDim-1];
    };

    // Pressure is p = p0 * (1 - L*h/T0)^(g*M/(R*L)
    m_gasPressure = [T0, P, L, gMRL ] (const RealVect a_position) -> Real {
      return P * std::pow(( 1 - L*a_position[SpaceDim-1]/T0), gMRL);
    };

    // Density is rho = P*Na/(T*R)
    m_gasDensity = [&p = this->m_gasPressure, &T = this->m_gasTemperature] (const RealVect a_position) -> Real {
      return (p(a_position) * Units::atm2pascal * Units::Na)/ (T(a_position) * Units::R);          
    };
  }
  else if(gasLaw == "table"){
    // These fields are required

    if(!gasJSON.contains("file"       )) this->throwParserError(baseError + " and got 'table' gas law but field 'file' is not specified"       );
    if(!gasJSON.contains("height"     )) this->throwParserError(baseError + " and got 'table' gas law but field 'height' is not specified"     );
    if(!gasJSON.contains("temperature")) this->throwParserError(baseError + " and got 'table' gas law but field 'temperature' is not specified");
    if(!gasJSON.contains("pressure"   )) this->throwParserError(baseError + " and got 'table' gas law but field 'pressure' is not specified"   );
    if(!gasJSON.contains("density"    )) this->throwParserError(baseError + " and got 'table' gas law but field 'density' is not specified"    );
    if(!gasJSON.contains("molar mass" )) this->throwParserError(baseError + " and got 'table' gas law but field 'molar mass' is not specified" );
    if(!gasJSON.contains("min height" )) this->throwParserError(baseError + " and got 'table' gas law but field 'min height' is not specified" );
    if(!gasJSON.contains("max height" )) this->throwParserError(baseError + " and got 'table' gas law but field 'max height' is not specified" );
    if(!gasJSON.contains("res height" )) this->throwParserError(baseError + " and got 'table' gas law but field 'res height' is not specified" );

    // Get the file name
    const auto filename  = trim(gasJSON["file"].get<std::string>());

    // Get columns where height, temperature, pressure, and density are specified. 
    const auto height    = gasJSON["height"     ].get<int>();
    const auto T         = gasJSON["temperature"].get<int>();
    const auto P         = gasJSON["pressure"   ].get<int>();
    const auto Rho       = gasJSON["density"    ].get<int>();

    // Get molar mass
    const auto M         = gasJSON["molar mass"].get<Real>();

    // Get table format specifications.
    const auto minHeight = gasJSON["min height"].get<Real>();
    const auto maxHeight = gasJSON["max height"].get<Real>();
    const auto resHeight = gasJSON["res height"].get<Real>();

    // It's an error if max height < min height
    if(maxHeight < minHeight) this->throwParserError(baseError + " and got tabulated gas law but can't have 'max height' < 'min height'");

    // Throw an error if the file does not exist. 
    if(!(this->doesFileExist(filename))) this->throwParserError(baseError + " and got tabulated gas law but file = '" + filename + "' was not found");

    // Let the data parser read the input. 
    LookupTable<2> temperatureTable = DataParser::simpleFileReadASCII(filename, height, T  );
    LookupTable<2> pressureTable    = DataParser::simpleFileReadASCII(filename, height, P  );
    LookupTable<2> densityTable     = DataParser::simpleFileReadASCII(filename, height, Rho);

    // Compute the number of points in the table
    const int numPoints = std::ceil((maxHeight - minHeight)/resHeight);

    // The input density was in kg/m^3 but we want the number density.
    densityTable.scale<1>(Units::Na/(M * g2kg));

    // Make the tables uniform
    temperatureTable.setRange(minHeight, maxHeight, 0);
    pressureTable.   setRange(minHeight, maxHeight, 0);
    densityTable.    setRange(minHeight, maxHeight, 0);
    
    temperatureTable.sort(0);
    pressureTable.   sort(0);
    densityTable.    sort(0);

    temperatureTable.makeUniform(numPoints);
    pressureTable.   makeUniform(numPoints);
    densityTable.    makeUniform(numPoints);

    // Now create the temperature, pressure, and density functions. 
    m_gasTemperature = [table = temperatureTable] (const RealVect a_position) -> Real {
      return table.getEntry<1>(a_position[SpaceDim-1]);
    };

    m_gasPressure = [table = pressureTable] (const RealVect a_position) -> Real {
      return table.getEntry<1>(a_position[SpaceDim-1]);
    };

    m_gasDensity = [table = densityTable] (const RealVect a_position) -> Real {
      return table.getEntry<1>(a_position[SpaceDim-1]);
    };
  }
  else{
    this->throwParserError(baseError + " -- logic bust");
  }

  // Instantiate the species densities. Note that we need to go through this twice because we need to normalize the molar fractions in case users
  // were a bit inconsiderate when setting them. 
  Real molarSum = 0.0;
  for (const auto& species : gasJSON["neutral species"]){
    if(!(species.contains("name")))           this->throwParserError(baseError + " in the array 'neutral species' the field 'name' is also required"          );
    if(!(species.contains("molar fraction"))) this->throwParserError(baseError + " in the array 'neutral species' the field 'molar fraction' is also required");

    molarSum += species["molar fraction"].get<Real>();
  }

  // Initialize the species. This will iterate through the neutral species in the JSON input file. 
  for (const auto& species : gasJSON["neutral species"]){
    const std::string speciesName     = trim(species["name"          ].get<std::string>());
    const Real        speciesFraction =      species["molar fraction"].get<Real>() / molarSum;

    // Names can not contain the at letter.
    if(containsWildcard(speciesName)) this->throwParserError(baseError + " -- species name must not contain '@' letter");

    // It's an error if a species was defined twice.
    if(isNeutralSpecies(speciesName)) this->throwParserError(baseError + " -- Neutral species '" + speciesName + "' was defined more than once");        

    // Set the species density function. 
    const std::function<Real(const RealVect)> speciesDensity  = [f = speciesFraction, &N = this->m_gasDensity] (const RealVect a_position) {
      return f * N(a_position);
    };

    // Add the species. Make sure the maps are consist.
    const int idx = m_neutralSpecies.size();
    
    // Create the neutral species (and the mapped background density).
    m_neutralSpecies.         push_back(std::shared_ptr<NeutralSpeciesJSON>((new NeutralSpeciesJSON(speciesName, speciesFraction, speciesDensity))));
    m_neutralSpeciesDensities.push_back(speciesDensity);
    
    // Create the string-int maps
    m_neutralSpeciesMap.       insert(std::make_pair(speciesName, idx        ));
    m_neutralSpeciesInverseMap.insert(std::make_pair(idx ,        speciesName));
  }

  // Figure out if we should plot the gas quantities
  m_plotGas = false;
  if(gasJSON.contains("plot")){
    m_plotGas = gasJSON["plot"].get<bool>();
  }
}

void CdrPlasmaJSON::initializePlasmaSpecies() {
  CH_TIME("CdrPlasmaJSON::initializePlasmaSpecies");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::initializePlasmaSpecies()" << endl;
  }

  const std::string baseError = "CdrPlasmaJSON::initializePlasmaSpecies ";

  if(!(m_json.contains("plasma species"))) this->throwParserWarning(baseError + " -- did not find any plasma species");

  // Iterate through all species defined in the JSON file. 
  for (const auto& species : m_json["plasma species"]){
    if(!(species.contains("name"     ))) this->throwParserError(baseError + "-- all entries in the  'plasma species' array must have field 'name'"     );
    if(!(species.contains("Z"        ))) this->throwParserError(baseError + "-- all entries in the  'plasma species' array must have field 'Z'"        );
    if(!(species.contains("mobile"   ))) this->throwParserError(baseError + "-- all entries in the  'plasma species' array must have field 'mobile'"   );
    if(!(species.contains("diffusive"))) this->throwParserError(baseError + "-- all entries in the  'plasma species' array must have field 'diffusive'");

    const auto name        = trim(species["name"].     get<std::string>());
    const auto Z           =      species["Z"].        get<int        >() ;
    const auto mobile      =      species["mobile"].   get<bool       >() ;
    const auto diffusive   =      species["diffusive"].get<bool       >() ;

    // Does not get to contain at letter
    if(containsWildcard(name)) this->throwParserError(baseError + "but species '" + name + "' can not contain the '@' letter");

    // It's an error if the species was already defined. 
    if(isPlasmaSpecies(name)) this->throwParserError(baseError + "but plasma species '" + name + "' was defined more than once");
    
    const bool hasInitData = species.contains("initial data");

    // Get the initial data. 
    std::function<Real(const RealVect, const Real)> initFunc;

    if(hasInitData){
      initFunc = [this, baseError, data = species["initial data"]] (const RealVect a_point, const Real a_time) -> Real {
	Real ret = 0.0;

	// Add uniform density. 
	if(data.contains("uniform")){
	  ret += data["uniform"].get<Real>();
	}

	// Add gaussian seed.
	if(data.contains("gauss2")){
	  const auto& gauss2 = data["gauss2"];

	  // These fields must exist
	  if(!(gauss2.contains("amplitude"))) this->throwParserError(baseError + "and got gauss2 for initial data but field 'amplitude' is not specified");
	  if(!(gauss2.contains("radius"   ))) this->throwParserError(baseError + "and got gauss2 for initial data but field 'radius' is not specified"   );
	  if(!(gauss2.contains("position" ))) this->throwParserError(baseError + "and got gauss2 for initial data but field 'position' is not specified" );	  

	  // Fetch fields and make the friggin functionl
	  const Real     amplitude = gauss2["amplitude"].get<Real>();	  
	  const Real     radius    = gauss2["radius"   ].get<Real>();
	  const RealVect center    = RealVect(D_DECL(gauss2["position"][0].get<Real>(),
						     gauss2["position"][1].get<Real>(),
						     gauss2["position"][2].get<Real>()));
	  const RealVect delta      = center - a_point;

	  ret += amplitude * exp(-delta.dotProduct(delta)/(2*std::pow(radius,2)));
	}

	// Add super-Gaussian seed.
	if(data.contains("gauss4")){
	  const auto& gauss4 = data["gauss4"];

	  // These fields must exist
	  if(!(gauss4.contains("amplitude"))) this->throwParserError(baseError + "and got gauss4 for initial data but field 'amplitude' is not specified");
	  if(!(gauss4.contains("radius"   ))) this->throwParserError(baseError + "and got gauss4 for initial data but field 'radius' is not specified"   );
	  if(!(gauss4.contains("position" ))) this->throwParserError(baseError + "and got gauss4 for initial data but field 'position' is not specified" );	  

	  // Fetch fields and make the friggin functionl
	  const Real     amplitude = gauss4["amplitude"].get<Real>();	  
	  const Real     radius    = gauss4["radius"   ].get<Real>();
	  const RealVect center    = RealVect(D_DECL(gauss4["position"][0].get<Real>(),
						     gauss4["position"][1].get<Real>(),
						     gauss4["position"][2].get<Real>()));
	  const RealVect delta      = center - a_point;	  

	  ret += amplitude * exp(-std::pow(delta.dotProduct(delta),2)/(2*std::pow(radius, 4)));
	}

	return ret;
      };
    }
    else{
      initFunc = [](const RealVect a_point, const Real a_time) -> Real {
	return 0.0;
      };
    }

    initFunc = this->parsePlasmaSpeciesInitialData(species);

    // Print out a message if we're verbose.
    if(m_verbose){
      pout() << "CdrPlasmaJSON::initializePlasmaSpecies: instantiating species" << "\n"
	     << "\tName        = " << name        << "\n"
	     << "\tZ           = " << Z           << "\n"
	     << "\tMobile      = " << mobile      << "\n"
	     << "\tDiffusive   = " << diffusive   << "\n"
	     << "\tInitialData = " << hasInitData << "\n";    	
    }

    // Initialize the species.
    const int num = m_cdrSpecies.size();

    // Make the string-int map encodings. 
    m_cdrSpeciesMap.       emplace(std::make_pair(name, num ));
    m_cdrSpeciesInverseMap.emplace(std::make_pair(num , name));

    // Push the JSON entry and the new CdrSpecies to corresponding vectors. 
    m_cdrSpecies.    push_back(RefCountedPtr<CdrSpecies> (new CdrSpeciesJSON(name, Z, diffusive, mobile, initFunc)));
    m_cdrSpeciesJSON.push_back(species);
  }
}

CdrPlasmaJSON::InitialDataFunction CdrPlasmaJSON::parsePlasmaSpeciesInitialData(const json& a_json) const {
  CH_TIME("CdrPlasmaJSON::parsePlasmaSpeciesInitialData()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parsePlasmaSpeciesInitialData()" << endl;
  }

  const std::string species   = a_json["name"].get<std::string>();
  const std::string baseError = "CdrPlasmaJSON::parsePlasmaSpeciesInitialData for species '" + species + "' ";

  // This is the returned data function. 
  InitialDataFunction initFunc = [](const RealVect a_position, const Real a_time) {
    return 0.0;
  };

  // JSON object for the initial data field.
  json initData;
  if(a_json.contains("initial data")){
    initData = a_json["initial data"];

    bool addUniformData   = false;
    bool addGauss2        = false;
    bool addGauss4        = false;
    bool addHeightProfile = false;

    // Uniform density
    Real uniformDensity = 0.0;
  
    // Gaussian seeds
    std::vector<FunctionX> gauss2Functions;
    std::vector<FunctionX> gauss4Functions;

    // Pointer to height profile table
    LookupTable<2> heightProfile;

    // Set the uniform density
    if(initData.contains("uniform")){
      uniformDensity = initData["uniform"].get<Real>();
    }

    // Add Gaussian seeds
    if(initData.contains("gauss2")) {
      const json& gauss2Array = initData["gauss2"];

      for (const auto& gauss2 : gauss2Array){
	
	// These fields are required
	if(!(gauss2.contains("radius"   ))) this->throwParserError(baseError + "and found 'gauss2' array in initial data but 'radius' was not specified"   );
	if(!(gauss2.contains("amplitude"))) this->throwParserError(baseError + "and found 'gauss2' array in initial data but 'amplitude' was not specified");
	if(!(gauss2.contains("position" ))) this->throwParserError(baseError + "and found 'gauss2' array in initial data but 'position' was not specified" );

	// Get the parameters
	const Real     radius    = gauss2["radius"   ].get<Real>();
	const Real     amplitude = gauss2["amplitude"].get<Real>();	  	
	const RealVect center    = RealVect(D_DECL(gauss2["position"][0].get<Real>(),
						   gauss2["position"][1].get<Real>(),
						   gauss2["position"][2].get<Real>()));


	// Add a Gaussian seed function
	const Real radius2 = 2*radius*radius;
	auto gaussianFunction = [radius2, amplitude, center](const RealVect a_position) -> Real {
	  const RealVect delta = center - a_position;	  

	  return amplitude * exp(-delta.dotProduct(delta)/radius2);
	};
      
	gauss2Functions.emplace_back(gaussianFunction);
      }
    }

    // Add super-Gaussian seeds
    if(initData.contains("gauss4")) {
      const json& gauss4Array = initData["gauss4"];

      for (const auto& gauss4 : gauss4Array){
	
	// These fields are required
	if(!(gauss4.contains("radius"   ))) this->throwParserError(baseError + "and found 'gauss4' array in initial data but 'radius' was not specified"   );
	if(!(gauss4.contains("amplitude"))) this->throwParserError(baseError + "and found 'gauss4' array in initial data but 'amplitude' was not specified");
	if(!(gauss4.contains("position" ))) this->throwParserError(baseError + "and found 'gauss4' array in initial data but 'position' was not specified" );

	// Get the parameters
	const Real     radius    = gauss4["radius"   ].get<Real>();
	const Real     amplitude = gauss4["amplitude"].get<Real>();	  	
	const RealVect center    = RealVect(D_DECL(gauss4["position"][0].get<Real>(),
						   gauss4["position"][1].get<Real>(),
						   gauss4["position"][2].get<Real>()));


	// Add a Gaussian seed function
	const Real radius4 = 2*std::pow(radius, 4);
	auto gaussianFunction = [radius4, amplitude, center](const RealVect a_position) -> Real {
	  const RealVect delta  = center - a_position;
	  const Real     delta2 = delta.dotProduct(delta);

	  return amplitude * exp(-delta2*delta2/(radius4));
	};
      
	gauss4Functions.emplace_back(gaussianFunction);
      }
    }

    if(initData.contains("height profile")){
      const json& heightProfileJSON = initData["height profile"];

      // These fields are required
      if(!(heightProfileJSON.contains("file"      ))) this->throwParserError(baseError + "and found 'height profile' in initial data but 'file' was not specified"      );
      if(!(heightProfileJSON.contains("height"    ))) this->throwParserError(baseError + "and found 'height profile' in initial data but 'height' was not specified"    );
      if(!(heightProfileJSON.contains("density"   ))) this->throwParserError(baseError + "and found 'height profile' in initial data but 'density' was not specified"   );
      if(!(heightProfileJSON.contains("min height"))) this->throwParserError(baseError + "and found 'height profile' in initial data but 'min height' was not specified");
      if(!(heightProfileJSON.contains("max height"))) this->throwParserError(baseError + "and found 'height profile' in initial data but 'max height' was not specified");
      if(!(heightProfileJSON.contains("res height"))) this->throwParserError(baseError + "and found 'height profile' in initial data but 'res height' was not specified");

      // Get the file name
      const auto filename  = trim(heightProfileJSON["file"].get<std::string>());

      const auto heightColumn  = heightProfileJSON["height"    ].get<int>();
      const auto densityColumn = heightProfileJSON["density"   ].get<int>();
      
      const auto minHeight     = heightProfileJSON["min height"].get<Real>();
      const auto maxHeight     = heightProfileJSON["max height"].get<Real>();
      const auto resHeight     = heightProfileJSON["res height"].get<Real>();

      // Fetch a scaling factor if it exists.
      Real scaleX = 1.0;
      Real scaleY = 1.0;
      
      if(heightProfileJSON.contains("scale height")){
	scaleX = heightProfileJSON["scale height"].get<Real>();
      }
      if(heightProfileJSON.contains("scale density")){
	scaleY = heightProfileJSON["scale density"].get<Real>();
      }

      // Can't have max height < min height.
      if(maxHeight < minHeight) this->throwParserError(baseError + " and found 'height profile' in initial data but can't have 'max height' < 'min height'");

      // Throw a warning if the input file does not exist. 
      if(!(this->doesFileExist(filename))) this->throwParserError(baseError + " and found 'height profile' in initial data but file = '" + filename + "' was not found");

      // Compute the number of points for the table
      const int numPoints = std::ceil((maxHeight-minHeight)/resHeight);

      // Parse input file into our nifty little LookupTable. 
      heightProfile = DataParser::simpleFileReadASCII(filename, heightColumn, densityColumn);

      // Sort the table along the first column. This is the height above ground.
      heightProfile.setRange(minHeight, maxHeight, 0);      

      // Scale the table by the desired factors. 
      heightProfile.scale<0>(scaleX);
      heightProfile.scale<1>(scaleY);

      // Set range and make the table uniform so we can use fast lookup.
      heightProfile.sort(0);      
      heightProfile.makeUniform(numPoints);
    }

    // Set the friggin function.
    initFunc = [uniformDensity, gauss2Functions, gauss4Functions, heightProfile] (const RealVect a_position, const Real a_time) {
      Real retVal = uniformDensity;

      // Add contribution from Gaussian seeds
      for (const auto& f : gauss2Functions) {
	retVal += f(a_position);
      }

      // Add contribution from super-Gaussian seeds
      for (const auto& f : gauss4Functions) {
	retVal += f(a_position);
      }

      // Add contribution from height profile.
      if(heightProfile.getNumEntries() > 0){
	retVal += heightProfile.getEntry<1>(a_position[SpaceDim-1]);
      }
      
      return retVal;
    };
  }

  return initFunc;
}

void CdrPlasmaJSON::initializePhotonSpecies() {
  CH_TIME("CdrPlasmaJSON::initializePhotonSpecies()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::initializePhotonSpecies()" << endl;
  }

  const std::string baseError = "CdrPlasmaJSON::initializePhotonSpecies ";

  if(!(m_json.contains("photon species"))) this->throwParserWarning(baseError + " -- did not find any photon species");

  for (const auto& species : m_json["photon species"]){
    if(!(species.contains("name" ))) this->throwParserError(baseError + "-- every entry in 'photon species' must have field 'name'" );
    if(!(species.contains("kappa"))) this->throwParserError(baseError + "-- every entry in 'photon species' must have field 'kappa'");

    const auto name  = trim(species["name" ].get<std::string>());
    const auto kappa = trim(species["kappa"].get<std::string>());

    // Does not get to contain at letter.
    if(containsWildcard(name)) this->throwParserError(baseError + " -- photon species name cannot contain '@'");

    // It's an error if the species is already defined.
    if(isPhotonSpecies(name)) this->throwParserError(baseError + "photon species '" + name + "' was defined more than once");    

    // Set the kappa-function needed by RteSpeciesJSON.
    std::function<Real(const RealVect a_position)> kappaFunction = [](const RealVect a_position) -> Real {return 1.0;};
    
    if(kappa == "constant"){
      if(!(species.contains("value"))) this->throwParserError(baseError + " -- got 'constant' kappa but field 'value' is missing");

      const Real value = species["value"].get<Real>();

      kappaFunction = [value](const RealVect a_position) {return value;};
    }
    else if (kappa == "helmholtz"){
      // For the Bourdon model the translation between the Eddington and Bourdon model is
      //
      //    kappa = pX*lambda/sqrt(3).
      //
      // where pX is the partial pressure for a species. This requires that we are able to compute the partial pressure. Fortunately, we have m_gasPressure
      // which computes the pressure, and m_neutralSpecies also stores the molar fraction for each species so this is comparatively easy to reconstruct.     

      // Make sure that 'lambda' is found in the photon species and that O2 is found in the neutral species. 
      if(!(species.contains("lambda" ))) this->throwParserError(baseError + " -- got 'helmholtz' kappa but field 'lambda' is missing" );
      if(!(species.contains("neutral"))) this->throwParserError(baseError + " -- got 'helmholtz' kappa but field 'neutral' is missing");

      // Get lambda and the neutral species that we base the partial pressure upon. 
      const auto neutral = trim(species["neutral"].get<std::string>());
      const auto lambda  =      species["lambda" ].get<Real       >() ;      

      // Make sure that the neutral is in the list of species. 
      if(m_neutralSpeciesMap.find(neutral) == m_neutralSpeciesMap.end()) {
	this->throwParserError(baseError + " -- got 'helmholtz' kappa but '" + neutral + "' is not in the list of neutral species");
      }

      // Get the molar fraction for this specific neutral
      const Real molarFraction = m_neutralSpecies[m_neutralSpeciesMap.at(neutral)]->getMolarFraction();

      kappaFunction = [lambda, P = this->m_gasPressure, m = molarFraction](const RealVect a_position) -> Real {
	return m*P(a_position)*lambda/sqrt(3.0);
      };
    }
    else if (kappa == "stochastic A"){
      // For the Chanrion stochastic model we compute the absorption length as
      //
      //    kappa = k1 * (k2/k1)^((f-f1)/(f2-f1))
      //
      // where k1 = chi min * p(neutral) and p(neutral) is the partial pressure (in Pa) for some species.
      //
      // Note that the frequency f is sampled stochastically. 
      //
      // This requires that we are able to compute the partial pressure.
      // Fortunately, we have m_gasPressure which computes the pressure, and m_neutralSpecies also stores the molar fraction for each species so this is comparatively
      // easy to reconstruct.
      
      if(!(species.contains("f1"     ))) this->throwParserError(baseError + " -- got 'stochastic A' but field 'f1' is missing"     );
      if(!(species.contains("f2"     ))) this->throwParserError(baseError + " -- got 'stochastic A' but field 'f2' is missing"     );
      if(!(species.contains("chi min"))) this->throwParserError(baseError + " -- got 'stochastic A' but field 'chi max' is missing");
      if(!(species.contains("chi min"))) this->throwParserError(baseError + " -- got 'stochastic A' but field 'chi min' is missing");
      if(!(species.contains("neutral"))) this->throwParserError(baseError + " -- got 'stochastic A' but field 'neutral' is missing");

      const auto f1      =      species["f1"     ].get<Real       >() ;
      const auto f2      =      species["f2"     ].get<Real       >() ;
      const auto chi_min =      species["chi min"].get<Real       >() ;
      const auto chi_max =      species["chi max"].get<Real       >() ;
      const auto neutral = trim(species["neutral"].get<std::string>());

      // Make sure that the neutral is in the list of species. 
      if(m_neutralSpeciesMap.find(neutral) == m_neutralSpeciesMap.end()) {
	this->throwParserError(baseError + " -- got 'bourdon' but '" + neutral + "' is not in the list of neutral species");
      }

      // Get the molar fraction for the specified species. 
      const Real molarFraction = m_neutralSpecies[m_neutralSpeciesMap.at(neutral)]->getMolarFraction();

      std::uniform_real_distribution<Real> udist = std::uniform_real_distribution<Real>(f1, f2);	      

      // Create the absorption function. 
      kappaFunction = [f1, f2, udist, x1=chi_min, x2=chi_max, m=molarFraction, &P=this->m_gasPressure](const RealVect a_position) mutable -> Real {

	// Create a uniform distribution on the range [f1,f2]	
	const Real f = Random::get(udist);
	const Real a = (f-f1)/(f2-f1);

	const Real p  = m * P(a_position);
	const Real K1 = x1*p;
	const Real K2 = x2*p;

	return K1*std::pow(K2/K1, a);
      };
    }
    else{
      this->throwParserError(baseError + " -- logic bust");
    }

    // Initialize the species.
    const int num = m_rtSpecies.size();

    // Make the string-int map encodings. 
    m_rteSpeciesMap.       emplace(std::make_pair(name, num ));
    m_rteSpeciesInverseMap.emplace(std::make_pair(num , name));

    // Push the JSON entry and the new CdrSpecies to corresponding vectors. 
    m_rtSpecies.     push_back(RefCountedPtr<RtSpecies> (new RteSpeciesJSON(name, kappaFunction)));
    m_rteSpeciesJSON.push_back(species);    
  }
}

void CdrPlasmaJSON::initializeSigma() {
  CH_TIME("CdrPlasmaJSON::initializeSigma()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::initializeSigma()" << endl;
  }

  m_initialSigma = [](const RealVect a_pos, const Real a_time) -> Real {
    return 0.0;
  };

  if(m_json.contains("sigma")){
    if(m_json["sigma"].contains("initial density")){
      const Real sigma = m_json["sigma"]["initial density"].get<Real>();
      
      m_initialSigma = [sigma] (const RealVect a_position, const Real a_time) -> Real {
	return sigma;
      };
    }
  }
}

void CdrPlasmaJSON::parseAlpha(){
  CH_TIME("CdrPlasmaJSON::parseAlpha()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseAlpha()" << endl;
  }

  const std::string baseError = "CdrPlasmaJSON::parseAlpha";

  // We must have a field 'alpha'
  if(!(m_json.contains("alpha"))) {
    this->throwParserError(baseError + " - input file does not contain field 'alpha'");
  }

  const json& alpha = m_json["alpha"];

  if(!alpha.contains("lookup")) {
    this->throwParserError(baseError + " field 'lookup' not specified");
  }

  const std::string lookup = alpha["lookup"].get<std::string>();

  // If we made it here we're good.
  if(lookup == "table E/N"){
    if(!(alpha.contains("file"   ))) this->throwParserError(baseError + " and got 'table E/N' but field 'file' is missing"   );
    if(!(alpha.contains("header" ))) this->throwParserError(baseError + " and got 'table E/N' but field 'header' is missing" );
    if(!(alpha.contains("E/N"    ))) this->throwParserError(baseError + " and got 'table E/N' but field 'E/N' is missing"    );
    if(!(alpha.contains("alpha/N"))) this->throwParserError(baseError + " and got 'table E/N' but field 'alpha/N' is missing");
    if(!(alpha.contains("min E/N"))) this->throwParserError(baseError + " and got 'table E/N' but field 'min E/N' is missing");
    if(!(alpha.contains("max E/N"))) this->throwParserError(baseError + " and got 'table E/N' but field 'max E/N' is missing");
    if(!(alpha.contains("points" ))) this->throwParserError(baseError + " and got 'table E/N' but field 'points' is missing" );
    if(!(alpha.contains("spacing"))) this->throwParserError(baseError + " and got 'table E/N' but field 'spacing' is missing");    
	
    const std::string filename  = this->trim(alpha["file"   ].get<std::string>());
    const std::string spacing   = this->trim(alpha["spacing"].get<std::string>());
    const std::string startRead = this->trim(alpha["header" ].get<std::string>());    
    const std::string stopRead  = "";

    const int  xColumn   = alpha["E/N"    ].get<int >();
    const int  yColumn   = alpha["alpha/N"].get<int >();
    const int  numPoints = alpha["points" ].get<int >();
    const Real minEN     = alpha["min E/N"].get<Real>();
    const Real maxEN     = alpha["max E/N"].get<Real>();

    // I can't have maxEN < minEN. Throw an error.
    if(maxEN < minEN) this->throwParserError(baseError + " and got 'table E/N' but can't have 'max E/N' < 'min E/N'");

    // Throw an error if the file does not exist. 
    if(!(this->doesFileExist(filename))) this->throwParserError(baseError + " and got 'table E/N' but file '" + filename + "' does not exist");

    // Read the table and format it. We happen to know that this function reads data into the approprate columns. So if
    // the user specified the correct E/N column then that data will be put in the first column. The data for mu*N will be in the
    // second column. 
    m_alphaTableEN = DataParser::fractionalFileReadASCII(filename, startRead, stopRead, xColumn, yColumn);

    // If the table is empty then it's an error.
    if(m_alphaTableEN.getNumEntries() == 0){
      this->throwParserError(baseError + " and got 'table E/N' but table is empty. This is probably an error");
    }

    // Figure out the table spacing
    TableSpacing tableSpacing;
    if(spacing == "uniform"){
      tableSpacing = TableSpacing::Uniform;
    }
    else if(spacing == "exponential"){
      tableSpacing = TableSpacing::Exponential;
    }
    else{
      this->throwParserError(baseError + "and got 'table E/N' but 'spacing' field = '" + spacing + "' which is not supported");
    }    

    // Format the table
    m_alphaTableEN.setRange(minEN, maxEN, 0);
    m_alphaTableEN.sort(0);
    m_alphaTableEN.setTableSpacing(tableSpacing);
    m_alphaTableEN.makeUniform(numPoints);

    // Check if we should dump the table to file so that users can debug.
    if(alpha.contains("dump")){
      const std::string dumpFile = alpha["dump"].get<std::string>();
      m_alphaTableEN.dumpTable(dumpFile);
    }    

    m_alphaLookup = LookupMethod::TableEN;
  }
  else{
    this->throwParserError(baseError + " but lookup specification '" + lookup + "' is not supported.");
  }

  // Check if we should plot alpha.
  m_plotAlpha = false;
  if(alpha.contains("plot")){
    m_plotAlpha = alpha["plot"].get<bool>();
  }
}

void CdrPlasmaJSON::parseEta(){
  CH_TIME("CdrPlasmaJSON::parseEta()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseEta()" << endl;
  }

  const std::string baseError = "CdrPlasmaJSON::parseEta";

  // We must have a field 'eta'
  if(!(m_json.contains("eta"))) {
    this->throwParserError(baseError + " - input file does not contain field 'eta'");
  }

  const json& eta = m_json["eta"];

  if(!eta.contains("lookup")) {
    this->throwParserError(baseError + " field 'lookup' not specified");
  }

  // Get the lookup method. 
  const std::string lookup = eta["lookup"].get<std::string>();

  // If we made it here we're good.
  if(lookup == "table E/N"){
    if(!(eta.contains("file"   ))) this->throwParserError(baseError + " and got 'table E/N' but field 'file' is missing"   );
    if(!(eta.contains("header" ))) this->throwParserError(baseError + " and got 'table E/N' but field 'header' is missing" );
    if(!(eta.contains("E/N"    ))) this->throwParserError(baseError + " and got 'table E/N' but field 'E/N' is missing"    );
    if(!(eta.contains("eta/N"  ))) this->throwParserError(baseError + " and got 'table E/N' but field 'eta/N' is missing"  );
    if(!(eta.contains("min E/N"))) this->throwParserError(baseError + " and got 'table E/N' but field 'min E/N' is missing");
    if(!(eta.contains("max E/N"))) this->throwParserError(baseError + " and got 'table E/N' but field 'max E/N' is missing");
    if(!(eta.contains("points" ))) this->throwParserError(baseError + " and got 'table E/N' but field 'points' is missing" );
    if(!(eta.contains("spacing"))) this->throwParserError(baseError + " and got 'table E/N' but field 'spacing' is missing");
    
    const std::string filename  = this->trim(eta["file"   ].get<std::string>());
    const std::string spacing   = this->trim(eta["spacing"].get<std::string>());
    const std::string startRead = this->trim(eta["header" ].get<std::string>());    
    const std::string stopRead  = "";    
	
    const int  xColumn   = eta["E/N"    ].get<int >();
    const int  yColumn   = eta["eta/N"  ].get<int >();
    const int  numPoints = eta["points" ].get<int >();
    const Real minEN     = eta["min E/N"].get<Real>();
    const Real maxEN     = eta["max E/N"].get<Real>();

    // Can't have maxEN < min EN
    if(maxEN < minEN) this->throwParserError(baseError + " and got 'table E/N' but can't have 'max E/N' < 'min E/N'");

    // Throw an error if the file does not exist. 
    if(!(this->doesFileExist(filename))) this->throwParserError(baseError + " and got 'table E/N' but file '" + filename + "' does not exist");    

    // Read the table and format it. We happen to know that this function reads data into the approprate columns. So if
    // the user specified the correct E/N column then that data will be put in the first column. The data for mu*N will be in the
    // second column. 
    m_etaTableEN = DataParser::fractionalFileReadASCII(filename, startRead, stopRead, xColumn, yColumn);

    // If the table is empty then it's an error.
    if(m_etaTableEN.getNumEntries() == 0){
      this->throwParserError(baseError + " and got 'table E/N' but table is empty. This is probably an error");
    }

    // Figure out the table spacing
    TableSpacing tableSpacing;
    if(spacing == "uniform"){
      tableSpacing = TableSpacing::Uniform;
    }
    else if(spacing == "exponential"){
      tableSpacing = TableSpacing::Exponential;
    }
    else{
      this->throwParserError(baseError + "and got 'table E/N' but 'spacing' field = '" + spacing + "' which is not supported");
    }        

    // Format the table
    m_etaTableEN.setRange(minEN, maxEN, 0);
    m_etaTableEN.sort(0);
    m_etaTableEN.setTableSpacing(tableSpacing);    
    m_etaTableEN.makeUniform(numPoints);

    // Check if we should dump the table to file so that users can debug.
    if(eta.contains("dump")){
      const std::string dumpFile = eta["dump"].get<std::string>();
      m_etaTableEN.dumpTable(dumpFile);
    }        

    m_etaLookup = LookupMethod::TableEN;
  }
  else{
    this->throwParserError(baseError + " but lookup specification '" + lookup + "' is not supported.");
  }

  // Check if we should plot eta.
  m_plotEta = false;
  if(eta.contains("plot")){
    m_plotEta = eta["plot"].get<bool>();
  }
}

void CdrPlasmaJSON::parseMobilities() {
  CH_TIME("CdrPlasmaJSON::parseMobilities");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseMobilities" << endl;
  }

  const std::string baseError = "CdrPlasmaJSON::parseMobilities ";

  // Iterate through all tracked species.
  for (const auto& species : m_cdrSpeciesJSON){
    const std::string name = trim(species["name"].get<std::string>());
    const int         idx  = m_cdrSpeciesMap.at(name);

    if(m_cdrSpecies[idx]->isMobile()){

      // This is a required field. We use it for specifying the mobility. 
      if(!(species.contains("mobility"))) this->throwParserError(baseError + " and species '" + name + "' is mobile but JSON file does not contain field 'mobility'");
      const json& mobilityJSON = species["mobility"];

      // Get the mobility lookup method. This must either be a constant, a function, or a table. We parse these cases differently.
      const std::string lookup = trim(mobilityJSON["lookup"].get<std::string>());
	
      if(lookup == "constant"){
	// User specified a constant mobility. We look for a field 'value' in the JSON file and set the mobility from that. If the
	// field does not exist then it's an error. 
	if(!(mobilityJSON.contains("value"))) this->throwParserError(baseError + "and got constant mobility but field 'value' was not specified");

	const Real value = mobilityJSON["value"].get<Real>();

	m_mobilityLookup.   emplace(std::make_pair(idx, LookupMethod::Constant));
	m_mobilityConstants.emplace(std::make_pair(idx, value                 ));
      }
      else if(lookup == "table E/N"){
	if(!(mobilityJSON.contains("file"   ))) this->throwParserError(baseError + "and got tabulated mobility but field 'file' was not specified"   );
	if(!(mobilityJSON.contains("header" ))) this->throwParserError(baseError + "and got tabulated mobility but field 'header' was not specified" );
	if(!(mobilityJSON.contains("E/N"    ))) this->throwParserError(baseError + "and got tabulated mobility but field 'E/N' was not specified"    );
	if(!(mobilityJSON.contains("mu*N"   ))) this->throwParserError(baseError + "and got tabulated mobility but field 'mu*N' was not specified"   );
	if(!(mobilityJSON.contains("min E/N"))) this->throwParserError(baseError + "and got tabulated mobility but field 'min E/N' was not specified");
	if(!(mobilityJSON.contains("max E/N"))) this->throwParserError(baseError + "and got tabulated mobility but field 'max E/N' was not specified");
	if(!(mobilityJSON.contains("points" ))) this->throwParserError(baseError + "and got tabulated mobility but field 'points' was not specified" );
	if(!(mobilityJSON.contains("spacing"))) this->throwParserError(baseError + "and got tabulated mobility but field 'spacing' was not specified");	
	
	const std::string filename  = this->trim(mobilityJSON["file"   ].get<std::string>());
	const std::string startRead = this->trim(mobilityJSON["header" ].get<std::string>());
	const std::string spacing   = this->trim(mobilityJSON["spacing"].get<std::string>());
	const std::string stopRead  = "";

	const int  xColumn   = mobilityJSON["E/N"    ].get<int >();
	const int  yColumn   = mobilityJSON["mu*N"   ].get<int >();
	const int  numPoints = mobilityJSON["points" ].get<int >();
	const Real minEN     = mobilityJSON["min E/N"].get<Real>();
	const Real maxEN     = mobilityJSON["max E/N"].get<Real>();

	// Check if we should scale the table.
	Real scale = 1.0;
	if(mobilityJSON.contains("scale")){
	  scale = mobilityJSON["scale"].get<Real>();
	}

	// Can't have maxEN < minEN
	if(maxEN < minEN) this->throwParserError(baseError + "and got 'table E/N' but can't have 'max E/N' < 'min E/N'");

	// Issue an error if the file does not exist at all!
	if(!(this->doesFileExist(filename))) this->throwParserError(baseError + "and got 'table E/N' with file = '" + filename + "' but file was not found");	

	// Read the table and format it. We happen to know that this function reads data into the approprate columns. So if
	// the user specified the correct E/N column then that data will be put in the first column. The data for mu*N will be in the
	// second column. 
	LookupTable<2> mobilityTable = DataParser::fractionalFileReadASCII(filename, startRead, stopRead, xColumn, yColumn);

	// If the table is empty then it's an error.
	if(mobilityTable.getNumEntries() == 0){
	  this->throwParserError(baseError + " and got tabulated mobility but mobility table '" + startRead + "' in file '" + filename + "'is empty");
	}

	// Figure out the table spacing
	TableSpacing tableSpacing;
	if(spacing == "uniform"){
	  tableSpacing = TableSpacing::Uniform;
	}
	else if(spacing == "exponential"){
	  tableSpacing = TableSpacing::Exponential;
	}
	else{
	  this->throwParserError(baseError + "and got tabulated mobility but 'spacing' field = '" + spacing + "' which is not supported");
	}

	// Format the table appropriately. 
	mobilityTable.scale<1>(scale);		
	mobilityTable.setRange(minEN, maxEN, 0);
	mobilityTable.sort(0);
	mobilityTable.setTableSpacing(tableSpacing);
	mobilityTable.makeUniform(numPoints);

	// Check if we should dump the table to file so that users can debug.
	if(mobilityJSON.contains("dump")){
	  const std::string dumpFile = mobilityJSON["dump"].get<std::string>();
	  mobilityTable.dumpTable(dumpFile);
	}

	// Ok, put the table where it belongs. 
	m_mobilityLookup.  emplace(std::make_pair(idx, LookupMethod::TableEN));
	m_mobilityTablesEN.emplace(std::make_pair(idx, mobilityTable         ));
      }		
      else if (lookup == "functionEN A"){
	FunctionEN func;
	
	if(!(mobilityJSON.contains("c1"))) this->throwParserError(baseError + " and got 'functionEN A' for the mobility but field 'c1' was not specified");
	if(!(mobilityJSON.contains("c2"))) this->throwParserError(baseError + " and got 'functionEN A' for the mobility but field 'c2' was not specified");
	if(!(mobilityJSON.contains("c3"))) this->throwParserError(baseError + " and got 'functionEN A' for the mobility but field 'c3' was not specified");

	const Real A = mobilityJSON["c1"].get<Real>();
	const Real B = mobilityJSON["c2"].get<Real>();
	const Real C = mobilityJSON["c3"].get<Real>();

	func = [A, B, C](const Real a_E, const Real a_N) -> Real {return A * std::pow(a_E, B) / std::pow(a_N, C);};

	m_mobilityLookup.     emplace(std::make_pair(idx, LookupMethod::FunctionEN));
	m_mobilityFunctionsEN.emplace(std::make_pair(idx, func                     ));	
      }
      else{
	this->throwParserError(baseError + " -- logic bust");
      }
    }
  }
}

void CdrPlasmaJSON::parseDiffusion() {
  CH_TIME("CdrPlasmaJSON::parseDiffusion");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseDiffusion" << endl;
  }

  const std::string baseError = "CdrPlasmaJSON::parseDiffusion ";  

  // Iterate through all tracked species.
  for (const auto& species : m_cdrSpeciesJSON){
    const std::string name = trim(species["name"].get<std::string>());
    const int         idx  = m_cdrSpeciesMap.at(name);

    if(m_cdrSpecies[idx]->isDiffusive()){

      // This is a required field. We use it for specifying the mobility. 
      if(!(species.contains("diffusion"))) this->throwParserError(baseError + "and species '" + name + "' is diffusive but JSON file does not contain field 'diffusion'");
      const json& diffusionJSON = species["diffusion"];

      // Get the mobility lookup method. This must either be a constant, a function, or a table. We parse these cases differently.
      const std::string lookup = trim(diffusionJSON["lookup"].get<std::string>());
	
      if(lookup == "constant"){
	// User specified a constant mobility. We look for a field 'value' in the JSON file and set the mobility from that. If the
	// field does not exist then it's an error.
	if(!(diffusionJSON.contains("value"))) this->throwParserError(baseError + "and got constant diffusion but field 'value' was not specified");

	const Real value = diffusionJSON["value"].get<Real>();

	m_diffusionLookup.   emplace(std::make_pair(idx, LookupMethod::Constant));
	m_diffusionConstants.emplace(std::make_pair(idx, value                 ));
      }
      else if(lookup == "table E/N"){
	if(!(diffusionJSON.contains("file"   ))) this->throwParserError(baseError + "and got tabulated diffusion but field 'file' was not specified"   );
	if(!(diffusionJSON.contains("header" ))) this->throwParserError(baseError + "and got tabulated diffusion but field 'header' was not specified" );
	if(!(diffusionJSON.contains("E/N"    ))) this->throwParserError(baseError + "and got tabulated diffusion but field 'E/N' was not specified"    );
	if(!(diffusionJSON.contains("D*N"    ))) this->throwParserError(baseError + "and got tabulated diffusion but field 'D*N' was not specified"    );
	if(!(diffusionJSON.contains("min E/N"))) this->throwParserError(baseError + "and got tabulated diffusion but field 'min E/N' was not specified");
	if(!(diffusionJSON.contains("max E/N"))) this->throwParserError(baseError + "and got tabulated diffusion but field 'max E/N' was not specified");
	if(!(diffusionJSON.contains("points" ))) this->throwParserError(baseError + "and got tabulated diffusion but field 'points' was not specified" );
	if(!(diffusionJSON.contains("spacing"))) this->throwParserError(baseError + "and got tabulated diffusion but field 'spacing' was not specified");		
	
	const std::string filename  = this->trim(diffusionJSON["file"  ].get<std::string>());
	const std::string startRead = this->trim(diffusionJSON["header"].get<std::string>());
	const std::string spacing   = this->trim(diffusionJSON["spacing"].get<std::string>());	
	const std::string stopRead  = "";

	const int  xColumn   = diffusionJSON["E/N"    ].get<int >();
	const int  yColumn   = diffusionJSON["D*N"    ].get<int >();
	const int  numPoints = diffusionJSON["points" ].get<int >();		
	const Real minEN     = diffusionJSON["min E/N"].get<Real>();
	const Real maxEN     = diffusionJSON["max E/N"].get<Real>();

	// Can't have maxEN < minEN
	if(maxEN < minEN) this->throwParserError(baseError + "and got 'table E/N' but can't have 'max E/N' < 'min E/N'");

	// Issue an error if the file does not exist at all!
	if(!(this->doesFileExist(filename))) this->throwParserError(baseError + "and got 'table E/N' with file = '" + filename + "' but file was not found");

	// Read the table and format it. We happen to know that this function reads data into the approprate columns. So if
	// the user specified the correct E/N column then that data will be put in the first column. The data for D*N will be in the
	// second column. 
	LookupTable<2> diffusionTable = DataParser::fractionalFileReadASCII(filename, startRead, stopRead, xColumn, yColumn);

	// If the table is empty then it's an error.
	if(diffusionTable.getNumEntries() == 0){
	  this->throwParserError(baseError + " and got tabulated diffusion but diffusion table '" + startRead + "' in file '" + filename + "'is empty");	  
	}

	// Check if we should scale the table.
	Real scale = 1.0;
	if(diffusionJSON.contains("scale")){
	  scale = diffusionJSON["scale"].get<Real>();
	}

	// Figure out the table spacing
	TableSpacing tableSpacing;
	if(spacing == "uniform"){
	  tableSpacing = TableSpacing::Uniform;
	}
	else if(spacing == "exponential"){
	  tableSpacing = TableSpacing::Exponential;
	}
	else{
	  this->throwParserError(baseError + " and got tabulated diffusion but 'spacing' field = '" + spacing + "' which is not supported");
	}	

	// Format the table
	diffusionTable.scale<1>(scale);
	diffusionTable.setRange(minEN, maxEN, 0);
	diffusionTable.sort(0);
	diffusionTable.setTableSpacing(tableSpacing);	
	diffusionTable.makeUniform(numPoints);

	// Check if we should dump the table to file so that users can debug.
	if(diffusionJSON.contains("dump")){
	  const std::string dumpFile = diffusionJSON["dump"].get<std::string>();
	  diffusionTable.dumpTable(dumpFile);
	}	

	m_diffusionLookup.  emplace(std::make_pair(idx, LookupMethod::TableEN));
	m_diffusionTablesEN.emplace(std::make_pair(idx, diffusionTable       ));
      }
      else if (lookup == "functionEN A"){
	FunctionEN func;
	
	if(!(diffusionJSON.contains("c1"))) this->throwParserError(baseError + " and got 'functionEN A' for the diffusion but field 'c1' was not specified");
	if(!(diffusionJSON.contains("c2"))) this->throwParserError(baseError + " and got 'functionEN A' for the diffusion but field 'c2' was not specified");
	if(!(diffusionJSON.contains("c3"))) this->throwParserError(baseError + " and got 'functionEN A' for the diffusion but field 'c3' was not specified");

	const Real A = diffusionJSON["c1"].get<Real>();
	const Real B = diffusionJSON["c2"].get<Real>();
	const Real C = diffusionJSON["c3"].get<Real>();

	func = [A, B, C](const Real a_E, const Real a_N) -> Real {return A * std::pow(a_E, B) / std::pow(a_N, C);};

	m_diffusionLookup.     emplace(std::make_pair(idx, LookupMethod::FunctionEN));
	m_diffusionFunctionsEN.emplace(std::make_pair(idx, func                     ));	
      }      
      else{
	this->throwParserError(baseError + " -- logic bust");	
      }
    }
  }
}

void CdrPlasmaJSON::parseTemperatures() {
  CH_TIME("CdrPlasmaJSON::parseTemperatures");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseTemperatures" << endl;
  }

  // Go through each species and check if the temperature was specified. 
  for (const auto& species : m_cdrSpeciesJSON){
    const std::string name = trim(species["name"].get<std::string>());
    const int         idx  = m_cdrSpeciesMap.at(name);

    // If temperature is not specified -- we use the background gas temperature. 
    if(!(species.contains("temperature"))){
      m_temperatureLookup.   emplace(idx, LookupMethod::FunctionX);
      m_temperatureConstants.emplace(idx, m_gasTemperature       );
    }
    else{
      const json& S = species["temperature"];

      const std::string baseError = "CdrPlasmaJSON::parseTemperatures -- temperature for species '" + name + "' ";      

      // We MUST have a lookup field in order to determine how we compute the temperature for a species. 
      if(!(S.contains("lookup"))) this->throwParserError(baseError + "was specified but field 'lookup' is missing");

      // Figure out the lookup method. 
      const std::string lookup = trim(S["lookup"].get<std::string>());
      if(lookup == "constant"){
	if(!(S.contains("value"))) this->throwParserError(baseError + "was specified as 'constant' but field 'value' is missing");

	const Real value = S["value"].get<Real>();

	// Create a function which returns a constant value everywhere. 
	m_temperatureLookup.   emplace(idx, LookupMethod::FunctionX);
	m_temperatureConstants.emplace(idx, [value] (const RealVect a_psition) -> Real {return value;});
      }
      else if (lookup == "table E/N"){
	if(!(S.contains("file"   ))) this->throwParserError(baseError + "was specified as 'table E/N' but field 'file' is missing"   );
	if(!(S.contains("header" ))) this->throwParserError(baseError + "was specified as 'table E/N' but field 'header' is missing" );
	if(!(S.contains("E/N"    ))) this->throwParserError(baseError + "was specified as 'table E/N' but field 'E/N' is missing"    );
	if(!(S.contains("eV"     ))) this->throwParserError(baseError + "was specified as 'table E/N' but field 'eV' is missing"     );
	if(!(S.contains("min E/N"))) this->throwParserError(baseError + "was specified as 'table E/N' but field 'min E/N' is missing");
	if(!(S.contains("max E/N"))) this->throwParserError(baseError + "was specified as 'table E/N' but field 'max E/N' is missing");
	if(!(S.contains("points" ))) this->throwParserError(baseError + "was specified as 'table E/N' but field 'points' is missing" );
	if(!(S.contains("spacing"))) this->throwParserError(baseError + "was specified as 'table E/N' but field 'spacing' is missing");

	const std::string filename  = this->trim(S["file"   ].get<std::string>());
	const std::string startRead = this->trim(S["header" ].get<std::string>());
	const std::string spacing   = this->trim(S["spacing"].get<std::string>());	
	const std::string stopRead  = "";

	const int  xColumn   = S["E/N"    ].get<int >();
	const int  yColumn   = S["eV"     ].get<int >();
	const int  numPoints = S["points" ].get<Real>();
	const Real minEN     = S["min E/N"].get<Real>();
	const Real maxEN     = S["max E/N"].get<Real>();

	// Can't have maxEN < minEN
	if(maxEN < minEN) this->throwParserError(baseError + "and got 'table E/N' but can't have 'max E/N' < 'min E/N'");

	// Issue an error if the file does not exist at all!
	if(!(this->doesFileExist(filename))) this->throwParserError(baseError + "was specified as 'table E/N' but got file = '" + filename + "' was not found");

	// Check if we should scale the table.
	Real scale = 1.0;
	if(S.contains("scale")){
	  scale = S["scale"].get<Real>();
	}

	// Read the table and format it. We happen to know that this function reads data into the approprate columns. So if
	// the user specified the correct E/N column then that data will be put in the first column. The data for D*N will be in the
	// second column. 
	LookupTable<2> temperatureTable = DataParser::fractionalFileReadASCII(filename, startRead, stopRead, xColumn, yColumn);

	// If the table is empty then it's an error.
	if(temperatureTable.getNumEntries() == 0){
	  this->throwParserError(baseError + " but temperature table '" + startRead + "' in file '" + filename + "'is empty. This is probably an error");	  
	}

	// Figure out the table spacing
	TableSpacing tableSpacing;
	if(spacing == "uniform"){
	  tableSpacing = TableSpacing::Uniform;
	}
	else if(spacing == "exponential"){
	  tableSpacing = TableSpacing::Exponential;
	}
	else{
	  this->throwParserError(baseError +"and got tabulated mobility but 'spacing' field = '" + spacing + "' which is not supported");
	}	

	// Format the table
	temperatureTable.scale<1>(scale);		
	temperatureTable.setRange(minEN, maxEN, 0);
	temperatureTable.sort(0);
	temperatureTable.setTableSpacing(tableSpacing);	
	temperatureTable.makeUniform(numPoints);

	// Conversion factor is eV to Kelvin.
	temperatureTable.scale<1>( (2.0*Units::Qe) / (3.0*Units::kb) );

	// Check if we should dump the table to file so that users can debug.
	if(S.contains("dump")){
	  const std::string dumpFile = S["dump"].get<std::string>();
	  temperatureTable.dumpTable(dumpFile);
	}		

	m_temperatureLookup.  emplace(std::make_pair(idx, LookupMethod::TableEN));
	m_temperatureTablesEN.emplace(std::make_pair(idx, temperatureTable      ));	
      }
      else{
	this->throwParserError(baseError + " -- logic bust");
      }
    }
  }
}

void CdrPlasmaJSON::parsePlasmaReactions() {
  CH_TIME("CdrPlasmaJSON::parsePlasmaReactions()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parsePlasmaReactions()" << endl;
  }

  for (const auto& R : m_json["plasma reactions"]){
    if(!(R.contains("reaction"))) this->throwParserError("CdrPlasmaJSON::parsePlasmaReactions -- field 'reaction' is missing from one of the reactions");
    if(!(R.contains("lookup"  ))) this->throwParserError("CdrPlasmaJSON::parsePlasmaReactions -- field 'lookup' is missing from one of the reactions");

    const std::string reaction  = trim(R["reaction"].get<std::string>());
    const std::string baseError = "CdrPlasmaJSON::parsePlasmaReactions for reaction '" + reaction + "' ";    

    // Parse the reaction string to figure out the species involved in the reaction. Note that this CAN involve the species
    // wildcard @, in which case we need need to build a superset of reaction strings that we parse. We do that below. 
    std::vector<std::string> reactants;
    std::vector<std::string> products ;
    
    this->parseReactionString(reactants, products, reaction);

    // Parse wildcards and make the reaction sets. std::list<std::pair<std::vector<std::string>, std::vector<std::string> > >
    const auto reactionSets = this->parseReactionWildcards(reactants, products, R);

    // Now run through the superset of reactions encoded by the input string. Because of the @ character some of the rate functions
    // need special handling. 
    for (const auto& curReaction : reactionSets){
      const std::vector<std::string> curReactants = curReaction.first ;
      const std::vector<std::string> curProducts  = curReaction.second;

      // This is the reaction index for the current index. The reaction we are currently
      // dealing with is put in m_plasmaReactions[reactionIdex]. 
      const int reactionIndex = m_plasmaReactions.size();      

      // Make sure reaction string makes sense. 
      this->sanctifyPlasmaReaction(curReactants, curProducts, reaction);      

      // Parse the reaction parameters. 
      this->parsePlasmaReactionRate       (reactionIndex, R);
      this->parsePlasmaReactionScaling    (reactionIndex, R);
      this->parsePlasmaReactionPlot       (reactionIndex, R);
      this->parsePlasmaReactionDescription(reactionIndex, R);
      this->parsePlasmaReactionSoloviev   (reactionIndex, R);

      // Make the string-int encoding so we can encode the reaction properly. Then add the reaction to the pile. 
      std::list<int> plasmaReactants ;
      std::list<int> neutralReactants;
      std::list<int> photonReactants ;      
      std::list<int> plasmaProducts  ;
      std::list<int> neutralProducts ;
      std::list<int> photonProducts  ;            

      this->getReactionSpecies(plasmaReactants,
			       neutralReactants,
			       photonReactants,			       
			       plasmaProducts,
			       neutralProducts,			       
			       photonProducts,
			       curReactants,
			       curProducts);      

      // Now create the reaction -- note that plasma reactions don't use photon species on the left hand side of the
      // reaction, and it ignores neutral species on the right-hand side of the reaction.
      m_plasmaReactions.emplace_back(plasmaReactants, neutralReactants, plasmaProducts, photonProducts);
    }
  }
}

std::list<std::pair<std::vector<std::string>, std::vector<std::string> > > CdrPlasmaJSON::parseReactionWildcards(const std::vector<std::string>& a_reactants,
														 const std::vector<std::string>& a_products,
														 const json& a_R){
  CH_TIME("CdrPlasmaJSON::parseReactionWildcards()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseReactionWildcards()" << endl;
  }

  // This is what we return. A horrific creature.
  std::list<std::pair<std::vector<std::string>, std::vector<std::string> > > reactionSets;  

  // This is the reaction name. 
  const std::string reaction  = a_R["reaction"].get<std::string>();
  const std::string baseError = "CdrPlasmaJSON::parseReactionWildcards for reaction '" + reaction + "'";

  // Check if reaction string had a wildcard '@'. If it did we replace the wildcard with the corresponding species. This means that we need to
  // build additional reactions. 
  const bool containsWildcard = this->containsWildcard(reaction);
  
  if(containsWildcard){
    if(!(a_R.contains("@"))) {
      this->throwParserError(baseError + "got reaction wildcard '@' but array '@:' was specified");
    }

    // Get the wildcards array. 
    const std::vector<std::string> wildcards = a_R["@"].get<std::vector<std::string> >();

    // Go through the wildcards and replace appropriately. 
    for (const auto& w : wildcards){
      std::vector<std::string> curReactants;
      std::vector<std::string> curProducts;

      // Replace by wildcard in reactants.  	
      for (const auto& r : a_reactants){
	if(this->containsWildcard(r)){
	  curReactants.emplace_back(w);
	}
	else{
	  curReactants.emplace_back(r);
	}
      }

      // Replace by wildcard in reactants.  	
      for (const auto& p : a_products){
	if(this->containsWildcard(p)){
	  curProducts.emplace_back(w);
	}
	else{
	  curProducts.emplace_back(p);
	}
      }

      reactionSets.emplace_back(curReactants, curProducts);
    }
  }
  else{
    reactionSets.emplace_back(a_reactants, a_products);
  }

  return reactionSets;
}

void CdrPlasmaJSON::sanctifyPlasmaReaction(const std::vector<std::string>& a_reactants,
					   const std::vector<std::string>& a_products,
					   const std::string               a_reaction) const {
  CH_TIME("CdrPlasmaJSON::sanctifyPlasmaReaction()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::sanctifyPlasmaReaction()" << m_jsonFile << endl;
  }

  const std::string baseError = "CdrPlasmaJSON::sanctifyPlasmaReaction "; 

  // All reactants must be in the list of neutral species or in the list of plasma species
  for (const auto& r : a_reactants){
    if(!isPlasmaSpecies(r) && !isNeutralSpecies(r))
      this->throwParserError(baseError + "but I do not know reacting species '" + r + "' for reaction '" + a_reaction + "'");
  }

  // All products should be in the list of plasma or photon species. It's ok if users include a neutral species -- we will ignore it (but tell the user about it).
  for (const auto& p : a_products){
    if(!isPlasmaSpecies(p) && !isPhotonSpecies(p) && !isNeutralSpecies(p)){
      this->throwParserError(baseError + "but I do not know product species '" + p + "' for reaction '" + a_reaction + "'.");
    }
  }

  // Check for charge conservation
  int sumCharge = 0;
  for (const auto& r : a_reactants){
    if (m_cdrSpeciesMap.find(r) != m_cdrSpeciesMap.end()){
      sumCharge -= m_cdrSpecies[m_cdrSpeciesMap.at(r)]->getChargeNumber();
    }
  }
  for (const auto& p : a_products){
    if (m_cdrSpeciesMap.find(p) != m_cdrSpeciesMap.end()){
      sumCharge += m_cdrSpecies[m_cdrSpeciesMap.at(p)]->getChargeNumber();
    }
  }

  if(sumCharge != 0) {
    this->throwParserWarning("CdrPlasmaJSON::sanctifyPlasmaReaction -- charge not conserved for reaction '" + a_reaction + "'.");
  }
}

void CdrPlasmaJSON::sanctifyPhotoReaction(const std::vector<std::string>& a_reactants,
					  const std::vector<std::string>& a_products,
					  const std::string               a_reaction) const {
  CH_TIME("CdrPlasmaJSON::sanctifyPhotoReaction");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::sanctifyPhotoReaction" << m_jsonFile << endl;
  }

  const std::string baseError = "CdrPlasmaJSON::sanctifyPhotoReaction for reaction '" + a_reaction + "' ";

  // All reactants must be in the list of neutral species or in the list of photon species
  int numPhotonSpecies = 0;  
  for (const auto& r : a_reactants){
    const bool isPlasma  = this->isPlasmaSpecies (r);
    const bool isNeutral = this->isNeutralSpecies(r);
    const bool isPhoton  = this->isPhotonSpecies (r);

    if( isNeutral) this->throwParserError(baseError + "neutral species (" + r + ") not allowed on left-hand side");
    if( isPlasma ) this->throwParserError(baseError + "plasma species (" + r + ") not allowed on left-hand side");
    if(!isPhoton ) this->throwParserError(baseError + "I do not know species species '" + r + "' on left hand side");
    if( isPhoton ) numPhotonSpecies++;
  }

  // There can only be one photon species on the left-hand side of the reaction. 
  if(numPhotonSpecies != 1){
    this->throwParserError("CdrPlasmaJSON::sanctifyPhotoReaction -- only one photon species allowed on left-hand side of photo-reaction '" + a_reaction + "'");
  }

  // All products should be in the list of plasma, neutral, or photon species. 
  for (const auto& p : a_products){
    const bool isPlasma  = this->isPlasmaSpecies (p);
    const bool isNeutral = this->isNeutralSpecies(p);
    const bool isPhoton  = this->isPhotonSpecies (p);

    if( isPhoton              ) this->throwParserError(baseError + "photon species '" + p + "' not allowed on right hand side"); 
    if(!isPlasma && !isNeutral) this->throwParserError(baseError + "I do not know species '" + p + "' on right hand side"     );
  }

  // Check for charge conservation
  int sumCharge = 0;
  for (const auto& r : a_reactants){
    if(this->isPlasmaSpecies(r)) sumCharge -= m_cdrSpecies[m_cdrSpeciesMap.at(r)]->getChargeNumber();
  }
  for (const auto& p : a_products){
    if(this->isPlasmaSpecies(p)) sumCharge += m_cdrSpecies[m_cdrSpeciesMap.at(p)]->getChargeNumber();
  }

  if(sumCharge != 0) {
    this->throwParserError(baseError + "charge is not conserved!");
  }
}

void CdrPlasmaJSON::sanctifySurfaceReaction(const std::vector<std::string>& a_reactants,
					    const std::vector<std::string>& a_products,
					    const std::string               a_reaction) const {
  CH_TIME("CdrPlasmaJSON::sanctifySurfaceReaction");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::sanctifySurfaceReaction" << m_jsonFile << endl;
  }

  const std::string baseError = "CdrPlasmaJSON::sanctifySurfaceReaction for reaction '" + a_reaction + "' ";

  // Only plasma and RTE species allowed on the left hand side of the reaction. 
  for (const auto& r : a_reactants){
    const bool isPlasma  = this->isPlasmaSpecies (r);
    const bool isNeutral = this->isNeutralSpecies(r);
    const bool isPhoton  = this->isPhotonSpecies (r);

    // No neutrals allowed.
    if(isNeutral){
      this->throwParserError(baseError + "neutral species (" + r + ") not allowed on left-hand side");
    }

    // Unknown species not allowed either. 
    if(!isPlasma && !isNeutral && !isPhoton){
      this->throwParserError(baseError + "but I do not know species '" + r + "' on left hand side");
    }
  }

  // All products should be in the list of plasma, neutral, or photon species. 
  for (const auto& p : a_products){
    const bool isPlasma  = this->isPlasmaSpecies (p);
    const bool isNeutral = this->isNeutralSpecies(p);
    const bool isPhoton  = this->isPhotonSpecies (p);

    if(isPhoton ) this->throwParserError(baseError + "photon species '" + p + "' not allowed on right hand side");
    if(isNeutral) this->throwParserError(baseError + "neutral species '" + p + "' not allowed on right hand side");

    // Unknown species not allowed either. 
    if(!isPlasma && !isNeutral && !isPhoton){
      this->throwParserError(baseError + "but I do not know species '" + p + "' on right hand side");
    }
  }
}

void CdrPlasmaJSON::parsePlasmaReactionRate(const int a_reactionIndex, const json& a_R){
  CH_TIME("CdrPlasmaJSON::parsePlasmaReactionRate");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parsePlasmaReactionRate" << endl;
  }

  // These MUST exist because they are checked for in parsePlasmaReactions().
  const std::string reaction  = trim(a_R["reaction"].get<std::string>());
  const std::string lookup    = trim(a_R["lookup"  ].get<std::string>());
  const std::string baseError = "CdrPlasmaJSON::parsePlasmaReactionRate for reaction '" + reaction + "' ";
      
  // Figure out how we compute the reaction rate for this reaction. 
  if(lookup == "constant"){

    // Constant reaction rates are easy, just fetch it and put it where it belongs. 
    if(!(a_R.contains("rate"))) this->throwParserError(baseError + " and got 'constant' but did not get 'rate'");

    const Real k = a_R["rate"].get<Real>();

    /// Add the rate and lookup method. 
    m_plasmaReactionLookup.   emplace(std::make_pair(a_reactionIndex, LookupMethod::Constant));
    m_plasmaReactionConstants.emplace(std::make_pair(a_reactionIndex, k                     ));
  }
  else if(lookup == "alpha*v"){
    if(!(a_R.contains("species"))) this->throwParserError(baseError + "and got 'alpha*v' but field 'species' was not found");

    const std::string species = trim(a_R["species"].get<std::string>());

    if(!(this->isPlasmaSpecies(species))) this->throwParserError(baseError + "and got 'alpha*v' but species '" + species + "' is not a plasma species");

    m_plasmaReactionAlphaV.emplace(a_reactionIndex, m_cdrSpeciesMap.at(species));
    m_plasmaReactionLookup.emplace(a_reactionIndex, LookupMethod::AlphaV       );
  }
  else if(lookup == "eta*v"){
    if(!(a_R.contains("species"))) this->throwParserError(baseError + "and got 'eta*v' but field 'species' was not found");

    const std::string species = trim(a_R["species"].get<std::string>());

    if(!(this->isPlasmaSpecies(species))) this->throwParserError(baseError + "and got 'eta*v' but species '" + species + "' is not a plasma species");

    m_plasmaReactionEtaV.  emplace(a_reactionIndex, m_cdrSpeciesMap.at(species));
    m_plasmaReactionLookup.emplace(a_reactionIndex, LookupMethod::EtaV         );
  }  
  else if(lookup == "functionT1T2 A"){
    if(!(a_R.contains("T1"))) this->throwParserError(baseError + "and got 'functionT1T2 A' but field 'T1' was not found");
    if(!(a_R.contains("T2"))) this->throwParserError(baseError + "and got 'functionT1T2 A' but field 'T2' was not found");
    if(!(a_R.contains("c1"))) this->throwParserError(baseError + "and got 'functionT1T2 A' but field 'c1' was not found");
    if(!(a_R.contains("c2"))) this->throwParserError(baseError + "and got 'functionT1T2 A' but field 'c2' was not found");    

    const std::string speciesT1 = trim(a_R["T1"].get<std::string>());
    const std::string speciesT2 = trim(a_R["T2"].get<std::string>());

    const bool isPlasmaT1  = this->isPlasmaSpecies(speciesT1);
    const bool isPlasmaT2  = this->isPlasmaSpecies(speciesT2);

    const bool isNeutralT1 = this->isNeutralSpecies(speciesT1);
    const bool isNeutralT2 = this->isNeutralSpecies(speciesT2);      

    // Make sure that the specified species exist. 
    if(!isPlasmaT1 && !isNeutralT1) this->throwParserError(baseError + "and got function 'functionT1T2 A' but do not know species '" + speciesT1 + "'");
    if(!isPlasmaT2 && !isNeutralT2) this->throwParserError(baseError + "and got function 'functionT1T2 A' but do not know species '" + speciesT2 + "'");    

    // This syntax may look weird, but we need to know precisely which temperatures are involved in the reaction. In general the reaction rate
    // is just a function k = f(T1, T2) but T1 and T2 could be the temperatures for either a plasma or a neutral species. So, the plasmaReactionFunctionsT1T2 map
    // is a very weird one because only the temperatures are passed into the functions and not the species themselves. This also happens to be the correct
    // design because the temperatures themselves can be computed in many formats, and we just don't have a way of reconstructing them here. 
    int firstIndex  = -1;
    int secondIndex = -1;

    if(isPlasmaT1) firstIndex  = m_cdrSpeciesMap.at(speciesT1);
    if(isPlasmaT2) secondIndex = m_cdrSpeciesMap.at(speciesT2);

    const Real c1 = a_R["c1"].get<Real>();
    const Real c2 = a_R["c2"].get<Real>();

    FunctionTT functionT1T2 = [=](const Real a_T1, const Real a_T2) -> Real {
      return c1*std::pow(a_T1/a_T2, c2);
    };

    m_plasmaReactionFunctionsTT.emplace(a_reactionIndex, std::make_tuple(firstIndex, secondIndex, functionT1T2));
    m_plasmaReactionLookup.     emplace(a_reactionIndex, LookupMethod::FunctionTT);
  }
  else if (lookup == "table E/N"){
    if(!(a_R.contains("file"   ))) this->throwParserError(baseError + "and got 'table E/N' but field 'file' was not found"   );
    if(!(a_R.contains("header" ))) this->throwParserError(baseError + "and got 'table E/N' but field 'header' was not found" );
    if(!(a_R.contains("E/N"    ))) this->throwParserError(baseError + "and got 'table E/N' but field 'E/N' was not found"    );
    if(!(a_R.contains("rate"   ))) this->throwParserError(baseError + "and got 'table E/N' but field 'rate' was not found"   );
    if(!(a_R.contains("min E/N"))) this->throwParserError(baseError + "and got 'table E/N' but field 'min E/N' was not found");
    if(!(a_R.contains("max E/N"))) this->throwParserError(baseError + "and got 'table E/N' but field 'max E/N' was not found");
    if(!(a_R.contains("points" ))) this->throwParserError(baseError + "and got 'table E/N' but field 'points' was not found" );
    if(!(a_R.contains("spacing"))) this->throwParserError(baseError + "and got 'table E/N' but field 'spacing' was not found");    

    const std::string filename  = this->trim(a_R["file"   ].get<std::string>());
    const std::string spacing   = this->trim(a_R["spacing"].get<std::string>());    
    const std::string startRead = this->trim(a_R["header" ].get<std::string>());
    const std::string stopRead  = "";

    const int  xColumn   = a_R["E/N"    ].get<int >();
    const int  yColumn   = a_R["rate"   ].get<int >();
    const int  numPoints = a_R["points" ].get<int >();    
    const Real minEN     = a_R["min E/N"].get<Real>();
    const Real maxEN     = a_R["max E/N"].get<Real>();

    // It's an error if max E/N < min E/N
    if(maxEN < minEN) this->throwParserError(baseError + "and got 'table E/N' but can't have 'max E/N' < 'min E/N'");

    // Throw an error if the input file does not exist.
    if(!(this->doesFileExist(filename))) this->throwParserError(baseError + "and got 'table E/N' but file '" + filename + "' does not exist");

    // Read the table and format it. We happen to know that this function reads data into the approprate columns. So if
    // the user specified the correct E/N column then that data will be put in the first column. The data for D*N will be in the
    // second column. 
    LookupTable<2> reactionTable = DataParser::fractionalFileReadASCII(filename, startRead, stopRead, xColumn, yColumn);

    // If the table is empty then it's an error.
    if(reactionTable.getNumEntries() == 0){
      this->throwParserError(baseError + "and got 'table E/N' but table is empty. This is probably an error");
    }

    // Figure out the table spacing
    TableSpacing tableSpacing;
    if(spacing == "uniform"){
      tableSpacing = TableSpacing::Uniform;
    }
    else if(spacing == "exponential"){
      tableSpacing = TableSpacing::Exponential;
    }
    else{
      this->throwParserError(baseError + "and got 'table E/N' but 'spacing' field = '" + spacing + "' which is not supported");
    }        

    // Format the table. 
    reactionTable.setRange(minEN, maxEN, 0);
    reactionTable.sort(0);
    reactionTable.setTableSpacing(tableSpacing);
    reactionTable.makeUniform(numPoints);

    // Check if we should dump the table to file so that users can debug.
    if(a_R.contains("dump")){
      const std::string dumpFile = a_R["dump"].get<std::string>();
      reactionTable.dumpTable(dumpFile);
    }        

    // Add the tabulated rate and identifier. 
    m_plasmaReactionLookup.  emplace(std::make_pair(a_reactionIndex, LookupMethod::TableEN));
    m_plasmaReactionTablesEN.emplace(std::make_pair(a_reactionIndex, reactionTable         ));      
  }
  else{
    this->throwParserError(baseError + "but lookup = '" + lookup + "' is not recognized");
  }  
}

void CdrPlasmaJSON::parsePlasmaReactionScaling(const int a_index, const json& a_R) {
  CH_TIME("CdrPlasmaJSON::parsePlasmaReactionScaling()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parsePlasmaReactionScaling()" << endl;
  }

  // TLDR: This routine tries to figure out how we should scale reactions. In general, reactions are scaled parametrically by a function f = f(E,x)
  //       where E is the electric field magnitude and x is the physical position. This is completely general, but there are many, many ways that
  //       such functions could be formed. Currently, we put these functions as one of the following:
  //
  //       f = s
  //         s -> scaling
  // 
  //       f = s * pq/(p(x) + pq)
  //         s  -> scaling
  //         pq -> quenching pressure (in atm)
  //      
  //
  //       f = s * kr/(kr + kp + kq(N)) * nu * mu
  //          s  -> scaling
  //          kr -> radiation rate
  //          kp -> predissociation rate
  //          kq -> quenching rate
  //          nu -> photoionization efficiency
  //          mu -> excitation efficiency efficiency  

  // This is the reaction name. We need it for debugging.
  const std::string reaction  = a_R["reaction"].get<std::string>();
  const std::string baseError = "CdrPlasmaJSON::parsePlasmaReactionScaling for reaction '" + reaction + "' ";

  // Scaling factor for reaction. 
  Real scale = 1.0;

  // Quenching pressure in case that is specified. 
  Real pq    = 0.0;

  // Stuff for when "photoionization" is specified. 
  Real kr        = 0.0;
  Real kp        = 0.0;
  Real kqN       = 0.0;
  Real photoiEff = 0.0;
  Real exciteEff = 0.0;      

  // Flags for determining how to scale the reaction. 
  bool doPressureQuenching = false;
  bool doPhotoIonization   = false;

  // Get the scaling factor.
  if(a_R.contains("scale")) {
    scale = a_R["scale"].get<Real>();
  }      

  if(a_R.contains("quenching pressure")){
    doPressureQuenching = true;
    
    pq = a_R["quenching pressure"].get<Real>();
  }

  // Parse the photoionization field. 
  if(a_R.contains("photoionization")){
    const json& photoi = a_R["photoionization"];
	
    // These fields are required.
    if(!(photoi.contains("kr"  )))       this->throwParserError(baseError + "got 'photoionization' but field 'kr' is missing"        );
    if(!(photoi.contains("kp"  )))       this->throwParserError(baseError + "got 'photoionization' but field 'kp' is missing"        );
    if(!(photoi.contains("kq/N")))       this->throwParserError(baseError + "got 'photoionization' but field 'kq/N' is missing"      );
    if(!(photoi.contains("photoi eff"))) this->throwParserError(baseError + "got 'photoionization' but field 'photoi eff' is missing");
    if(!(photoi.contains("excite eff"))) this->throwParserError(baseError + "got 'photoionization' but field 'excite eff' is missing");
	
    kr        = photoi["kr"        ].get<Real>();
    kp        = photoi["kp"        ].get<Real>();
    kqN       = photoi["kq/N"      ].get<Real>();
    photoiEff = photoi["photoi eff"].get<Real>();
    exciteEff = photoi["excite eff"].get<Real>();	

    doPhotoIonization = true;
  }

  // This is the scaling function. It will be stored in m_plasmaReactionEfficiencies. 
  FunctionEX func;

  // Now make ourselves a lambda that we can use for scaling the reactions. 
  if(doPhotoIonization && doPressureQuenching) {
    this->throwParserError(baseError + "- cannot specify both 'photoionization' and 'quenching pressure'");
  }
  else if(doPressureQuenching && !doPhotoIonization){
    func = [scale, pq, p = this->m_gasPressure](const Real E, const RealVect x){
      return scale * pq/(pq + p(x));
    };
  }
  else if(!doPressureQuenching && doPhotoIonization){
    func = [scale, kr, kp, kqN, photoiEff, exciteEff, &N = this->m_gasDensity](const Real E, const RealVect x) -> Real {
      const Real kq = kqN * N(x);

      return scale * kr/(kr + kp + kq) * photoiEff * exciteEff;
    };
  }
  else {
    func = [scale](const Real E, const RealVect x) -> Real {
      return scale;
    };
  }

  // Add it to the pile. 
  m_plasmaReactionEfficiencies.emplace(a_index, func);
}

void CdrPlasmaJSON::parsePlasmaReactionPlot(const int a_reactionIndex, const json& a_R) {
  CH_TIME("CdrPlasmaJSON::parsePlasmaReactionPlot");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parsePlasmaReactionPlot" << endl;
  }

  bool plot = false;

  if(a_R.contains("plot")){
    plot = a_R["plot"].get<bool>();
  }

  m_plasmaReactionPlot.emplace(a_reactionIndex, plot);  
}

void CdrPlasmaJSON::parsePlasmaReactionDescription(const int a_reactionIndex, const json& a_R) {
  CH_TIME("CdrPlasmaJSON::parsePlasmaReactionDescription");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parsePlasmaReactionDescription" << endl;
  }

  // Reaction name. 
  std::string str = a_R["reaction"].get<std::string>();  

  // Determine if the reaction had a field "description". If it did, we will use that description in I/O files
  if(a_R.contains("description")){
    str = a_R["description"].get<std::string>();
  }

  m_plasmaReactionDescriptions.emplace(a_reactionIndex, str);        
}

void CdrPlasmaJSON::parsePlasmaReactionSoloviev(const int a_reactionIndex, const json& a_R) {
  CH_TIME("CdrPlasmaJSON::parsePlasmaReactionScaling");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parsePlasmaReactionScaling" << endl;
  }

  // TLDR: This will look through reactions and check if we should use the Soloviev energy correction for LFA-based models. 
  if(a_R.contains("soloviev")){
    
    const std::string reaction  = a_R["reaction"].get<std::string>();
    const std::string baseError = "CdrPlasmaJSON::parsePlasmaReactionSoloviev for '" + reaction + "' ";

    const json& solo = a_R["soloviev"];

    if(!solo.contains("correction")) this->throwParserError(baseError + "but did not find field 'correction'");
    if(!solo.contains("species"   )) this->throwParserError(baseError + "but did not find field 'species'"   );

    const auto correct = solo["correction"].get<bool       >();
    const auto species = solo["species"   ].get<std::string>();

    if(correct){
      if(!isPlasmaSpecies(species)) this->throwParserError(baseError + "but '" + species + "' is not a plasma species");

      const int plasmaSpecies = m_cdrSpeciesMap.at(species);

      const bool isMobile    = m_cdrSpecies[plasmaSpecies]->isMobile   ();
      const bool isDiffusive = m_cdrSpecies[plasmaSpecies]->isDiffusive();

      if(!isMobile   ) this->throwParserError(baseError + "but species  + '" + species + "' isn't mobile."   );
      if(!isDiffusive) this->throwParserError(baseError + "but species  + '" + species + "' isn't diffusive.");

      m_plasmaReactionSolovievCorrection.emplace(a_reactionIndex, std::make_pair(true, plasmaSpecies));
    }
    else{
      m_plasmaReactionSolovievCorrection.emplace(a_reactionIndex, std::make_pair(false, -1));
    }
  }
  else{
    m_plasmaReactionSolovievCorrection.emplace(a_reactionIndex, std::make_pair(false, -1));
  }
}



void CdrPlasmaJSON::parsePhotoReactions(){
  CH_TIME("CdrPlasmaJSON::parsePhotoReactions()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parsePhotoReactions()" << endl;
  }

  for (const auto& R : m_json["photo reactions"]){
    if(!(R.contains("reaction"))) this->throwParserError("CdrPlasmaJSON::parsePhotoReactions -- field 'reaction' is missing from one of the reactions");

    const std::string reaction = trim(R["reaction"].get<std::string>());

    std::vector<std::string> reactants;
    std::vector<std::string> products;

    // Index.
    const int reactionIndex = m_photoReactions.size();

    this->parseReactionString  (reactants, products, reaction);
    this->sanctifyPhotoReaction(reactants, products, reaction);


    // Parse if we use Helmholtz reconstruction for this photoionization method.
    this->parsePhotoReactionScaling(reactionIndex, R);

    // Make the string-int encoding so we can encode the reaction properly. Then add the reaction to the pile. 
    std::list<int> plasmaReactants ;
    std::list<int> neutralReactants;
    std::list<int> photonReactants ;
    std::list<int> plasmaProducts  ;
    std::list<int> neutralProducts ;
    std::list<int> photonProducts  ;    

    this->getReactionSpecies(plasmaReactants,
			     neutralReactants,
			     photonReactants,
			     plasmaProducts,
			     neutralProducts,
			     photonProducts,			     
			     reactants,
			     products);

    // Add the reaction to the pile. 
    m_photoReactions.emplace_back(plasmaReactants, neutralReactants, photonReactants, plasmaProducts, neutralProducts);
  }
}

void CdrPlasmaJSON::parsePhotoReactionScaling(const int a_reactionIndex, const json& a_R) {
  CH_TIME("CdrPlasmaJSON::parsePhotoReactionScale");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parsePhotoReactionScale" << endl;
  }

  // TLDR: This routine tries to figure out how we should scale photo-reactions. In general, reactions are scaled parametrically by a function f = f(E,x)
  //       where E is the electric field magnitude and x is the physical position. This is completely general, but there are many, many ways that
  //       such functions could be formed. Currently, we put these functions as one of the following:
  //
  //       f = s
  //         s -> scaling
  // 
  //       f = s * c * A * pX/(sqrt(3) * l)
  //         s  -> scaling
  //         c  -> Speed of light
  //         A  -> Helmholtz constants
  //         pX -> Partial pressure
  //         pq -> quenching pressure (in atm)

  // This is the reaction name. We need it for debugging.
  const std::string reaction  = a_R["reaction"].get<std::string>();
  const std::string baseError = "CdrPlasmaJSON::parsePhotoReactionScaling for reaction '" + reaction + "' ";

  // Regular scale
  Real scale = 1.0;

  // Parameters for the Helmholtz reconstruction. 
  Real helmholtzFactor = 0.0;
  bool doHelmholtz = false;

  if(a_R.contains("scale")){
    scale = a_R["scale"].get<Real>();
  }

  if(a_R.contains("helmholtz")){
    const json& helm = a_R["helmholtz"];

    if(!(helm.contains("A")))       this->throwParserError(baseError + " and got 'helmholtz' kappa but field 'A' is missing"      );
    if(!(helm.contains("lambda")))  this->throwParserError(baseError + " and got 'helmholtz' kappa but field 'lambda' is missing" );
    if(!(helm.contains("species"))) this->throwParserError(baseError + " and got 'helmholtz' kappa but field 'neutral' is missing");

    const auto A       = helm["A"      ].get<Real       >();
    const auto lambda  = helm["lambda" ].get<Real       >();
    const auto neutral = helm["species"].get<std::string>();
    
    // Get the specified species and it's molar fraction. 
    // Make sure that it's a neutral species.
    if(!(this->isNeutralSpecies(neutral))) {
      this->throwParserError(baseError + " and got 'helmholtz' kappa but species '" + neutral + "' is not a neutral species");
    }

    // Make the Helmholtz factor
    const Real molarFraction = m_neutralSpecies[m_neutralSpeciesMap.at(neutral)]->getMolarFraction();

    helmholtzFactor = (Units::c * A * molarFraction) / (sqrt(3.0) * lambda);

    doHelmholtz = true;
  }


  // Make the function object that we scale with. 
  FunctionEX func;

  if(doHelmholtz){
    func = [scale, helmholtzFactor, &p = this->m_gasPressure](const Real E, const RealVect x){
      return scale * helmholtzFactor * p(x);
    };
  }
  else{
    func = [scale] (const Real E, const RealVect x){
      return scale;
    };
  }

  m_photoReactionEfficiencies.emplace(a_reactionIndex, func       );
  m_photoReactionUseHelmholtz.emplace(a_reactionIndex, doHelmholtz);
}

void CdrPlasmaJSON::parseElectrodeReactions() {
  CH_TIME("CdrPlasmaJSON::parseElectrodeReactions()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseElectrodeReactions()" << endl;
  }

  // Check if our JSON f
  if(m_json.contains("electrode reactions")){
    const json& electrodeReactions = m_json["electrode reactions"];

    // Base error used when parsing the reactions
    const std::string baseError = "CdrPlasmaJSON::parseElectrodeReactions ";    

    // Iterate through the reactions. 
    for (const auto& electrodeReaction : electrodeReactions){

      // These fields are required
      if(!(electrodeReaction.contains("reaction"))) this->throwParserError(baseError + "- found 'electrode reactions' but field 'reaction' was not specified");
      if(!(electrodeReaction.contains("lookup"  ))) this->throwParserError(baseError + "- found 'electrode reactions' but field 'lookup' was not specified");      

      // Get the reaction and lookup strings. 
      const std::string reaction = this->trim(electrodeReaction["reaction"].get<std::string>());
      const std::string lookup   = this->trim(electrodeReaction["lookup"  ].get<std::string>());

      // Parse the reaction string so we get a list of reactants and products
      std::vector<std::string> reactants;
      std::vector<std::string> products ;

      this->parseReactionString(reactants, products, reaction);

      // Electrode reactions can contain wildcards -- if the current reaction contains a wildcard
      // we create a list of more reactions.
      const auto reactionSets = this->parseReactionWildcards(reactants, products, electrodeReaction);

      // Go through all the reactions now. 
      for (const auto& curReaction : reactionSets){

	const std::vector<std::string> curReactants = curReaction.first ;
	const std::vector<std::string> curProducts  = curReaction.second;

	// Sanctify the reaction -- make sure that all left-hand side and right-hand side species make sense.
	this->sanctifySurfaceReaction(curReactants, curProducts, reaction);

	// This is the reaction index for the current index. The reaction we are currently
	// dealing with is put in m_plasmaReactions[reactionIdex]. 
	const int reactionIndex = m_electrodeReactions.size();

	// Parse the scaling factor for the electrode surface reaction
	this->parseElectrodeReactionRate   (reactionIndex, electrodeReaction);	
	this->parseElectrodeReactionScaling(reactionIndex, electrodeReaction);

	// Make the string-int encoding so we can encode the reaction properly. Then add the reaction to the pile. 
	std::list<int> plasmaReactants ;
	std::list<int> neutralReactants;
	std::list<int> photonReactants ;      
	std::list<int> plasmaProducts  ;
	std::list<int> neutralProducts ;
	std::list<int> photonProducts  ;            

	this->getReactionSpecies(plasmaReactants,
				 neutralReactants,
				 photonReactants,			       
				 plasmaProducts,
				 neutralProducts,			       
				 photonProducts,
				 curReactants,
				 curProducts);

	// Now create the reaction -- note that surface reactions support both plasma species and photon species on
	// the left hand side of the reaction. 
	m_electrodeReactions.emplace_back(plasmaReactants, photonReactants, plasmaProducts);
      }
    }
  }
}

void CdrPlasmaJSON::parseElectrodeReactionRate(const int a_reactionIndex, const json& a_reactionJSON) {
  CH_TIME("CdrPlasmaJSON::parseElectrodeReactionRate()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseElectrodeReactionRate()" << endl;
  }

  // This is the reaction string -- we happen to know that, if we make it to this code, it exists.
  const std::string reaction  = trim(a_reactionJSON["reaction"].get<std::string>());

  // Create a basic error message for error handling. 
  const std::string baseError = "CdrPlasmaJSON::parseElectrodeReactionRate for reaction '" + reaction + "' ";

  // We MUST have a field lookup because it determines how we compute the efficiencies for surface reactions.
  if(!(a_reactionJSON.contains("lookup"))) this->throwParserError(baseError + "but field 'lookup' was not specified");

  // Get the lookup method
  const std::string lookup = this->trim(a_reactionJSON["lookup"].get<std::string>());

  // Now go through the various rate-computation methods and populated the
  // relevant data holders. 
  if(lookup == "constant"){
    // If using a constant emission rate, we must get the field 'value'.

    if(!(a_reactionJSON.contains("value"))) this->throwParserError(baseError + " and got 'constant' lookup but field 'value' is missing");

    const Real rate = a_reactionJSON["value"].get<Real>();

    // Add the constant reaction rate to the appropriate data holder. 
    m_electrodeReactionLookup.   emplace(a_reactionIndex, LookupMethod::Constant);
    m_electrodeReactionConstants.emplace(a_reactionIndex, rate                  );
  }
  else{
    this->throwParserError(baseError + "but lookup specification '" + lookup + "' is not supported");
  }
}

void CdrPlasmaJSON::parseElectrodeReactionScaling(const int a_reactionIndex, const json& a_reactionJSON) {
  CH_TIME("CdrPlasmaJSON::parseElectrodeReactionScaling()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseElectrodeReactionScaling()" << endl;
  }

  Real scale = 1.0;
  if(a_reactionJSON.contains("scale")){
    scale = a_reactionJSON["scale"].get<Real>();
  }

  // Create a function = scale everywhere. Extensions to scaling of more generic types of surface
  // reactions can be done by expanding this routine. 
  auto func = [scale](const Real E, const RealVect x) -> Real {
    return scale;
  };

  // Add it to the pile. 
  m_electrodeReactionEfficiencies.emplace(a_reactionIndex, func);
}

void CdrPlasmaJSON::parseDielectricReactions() {
  CH_TIME("CdrPlasmaJSON::parseDielectricReactions()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseDielectricReactions()" << endl;
  }

  // Check if our JSON f
  if(m_json.contains("dielectric reactions")){
    const json& dielectricReactions = m_json["dielectric reactions"];

    // Base error used when parsing the reactions
    const std::string baseError = "CdrPlasmaJSON::parseDielectricReactions ";    

    // Iterate through the reactions. 
    for (const auto& dielectricReaction : dielectricReactions){

      // These fields are required
      if(!(dielectricReaction.contains("reaction"))) this->throwParserError(baseError + "- found 'dielectric reactions' but field 'reaction' was not specified");
      if(!(dielectricReaction.contains("lookup"  ))) this->throwParserError(baseError + "- found 'dielectric reactions' but field 'lookup' was not specified");      

      // Get the reaction and lookup strings. 
      const std::string reaction = this->trim(dielectricReaction["reaction"].get<std::string>());
      const std::string lookup   = this->trim(dielectricReaction["lookup"  ].get<std::string>());

      // Parse the reaction string so we get a list of reactants and products
      std::vector<std::string> reactants;
      std::vector<std::string> products ;

      this->parseReactionString(reactants, products, reaction);

      // Dielectric reactions can contain wildcards -- if the current reaction contains a wildcard
      // we create a list of more reactions.
      const auto reactionSets = this->parseReactionWildcards(reactants, products, dielectricReaction);

      // Go through all the reactions now. 
      for (const auto& curReaction : reactionSets){

	const std::vector<std::string> curReactants = curReaction.first ;
	const std::vector<std::string> curProducts  = curReaction.second;

	// Sanctify the reaction -- make sure that all left-hand side and right-hand side species make sense.
	this->sanctifySurfaceReaction(curReactants, curProducts, reaction);

	// This is the reaction index for the current index. The reaction we are currently
	// dealing with is put in m_plasmaReactions[reactionIdex]. 
	const int reactionIndex = m_dielectricReactions.size();

	// Parse the scaling factor for the dielectric surface reaction
	this->parseDielectricReactionRate   (reactionIndex, dielectricReaction);	
	this->parseDielectricReactionScaling(reactionIndex, dielectricReaction);

	// Make the string-int encoding so we can encode the reaction properly. Then add the reaction to the pile. 
	std::list<int> plasmaReactants ;
	std::list<int> neutralReactants;
	std::list<int> photonReactants ;      
	std::list<int> plasmaProducts  ;
	std::list<int> neutralProducts ;
	std::list<int> photonProducts  ;            

	this->getReactionSpecies(plasmaReactants,
				 neutralReactants,
				 photonReactants,			       
				 plasmaProducts,
				 neutralProducts,			       
				 photonProducts,
				 curReactants,
				 curProducts);

	// Now create the reaction -- note that surface reactions support both plasma species and photon species on
	// the left hand side of the reaction. 
	m_dielectricReactions.emplace_back(plasmaReactants, photonReactants, plasmaProducts);
      }
    }
  }
}

void CdrPlasmaJSON::parseDielectricReactionRate(const int a_reactionIndex, const json& a_reactionJSON) {
  CH_TIME("CdrPlasmaJSON::parseDielectricReactionRate()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseDielectricReactionRate()" << endl;
  }

  // This is the reaction string -- we happen to know that, if we make it to this code, it exists.
  const std::string reaction  = trim(a_reactionJSON["reaction"].get<std::string>());

  // Create a basic error message for error handling. 
  const std::string baseError = "CdrPlasmaJSON::parseDielectricReactionRate for reaction '" + reaction + "' ";

  // We MUST have a field lookup because it determines how we compute the efficiencies for surface reactions.
  if(!(a_reactionJSON.contains("lookup"))) this->throwParserError(baseError + "but field 'lookup' was not specified");

  // Get the lookup method
  const std::string lookup = this->trim(a_reactionJSON["lookup"].get<std::string>());

  // Now go through the various rate-computation methods and populated the
  // relevant data holders. 
  if(lookup == "constant"){
    // If using a constant emission rate, we must get the field 'value'.

    if(!(a_reactionJSON.contains("value"))) this->throwParserError(baseError + " and got 'constant' lookup but field 'value' is missing");

    const Real rate = a_reactionJSON["value"].get<Real>();

    // Add the constant reaction rate to the appropriate data holder. 
    m_dielectricReactionLookup.   emplace(a_reactionIndex, LookupMethod::Constant);
    m_dielectricReactionConstants.emplace(a_reactionIndex, rate                  );
  }
  else{
    this->throwParserError(baseError + "but lookup specification '" + lookup + "' is not supported");
  }
}

void CdrPlasmaJSON::parseDielectricReactionScaling(const int a_reactionIndex, const json& a_reactionJSON) {
  CH_TIME("CdrPlasmaJSON::parseDielectricReactionScaling()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseDielectricReactionScaling()" << endl;
  }

  Real scale = 1.0;
  if(a_reactionJSON.contains("scale")){
    scale = a_reactionJSON["scale"].get<Real>();
  }

  // Create a function = scale everywhere. Extensions to scaling of more generic types of surface
  // reactions can be done by expanding this routine. 
  auto func = [scale](const Real E, const RealVect x) -> Real {
    return scale;
  };

  // Add it to the pile. 
  m_dielectricReactionEfficiencies.emplace(a_reactionIndex, func);
}

void CdrPlasmaJSON::parseDomainReactions(){
  CH_TIME("CdrPlasmaJSON::parseDomainReactions()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseDomainReactions()" << endl;
  }
  
  // Check if JSON file contains domain reactions
  if(m_json.contains("domain reactions")){
    const json& domainReactions = m_json["domain reactions"];

    // Base error when parsing the reactions
    const std::string baseError = "CdrPlasmaJSON::parseDomainReactions ";

    //const std::map<char, int> dirMap{{'x', 0}, {'y', 1}, {'z', 2}};
    //const std::map<std::string, Side::LoHiSide> sideMap{{"lo", Side::Lo}, {"hi", Side::Hi}};

    // Iterate through the reactions
    for (const auto& domainReaction : domainReactions){

      // These fields are required
      if(!(domainReaction.contains("reaction"))) this->throwParserError(baseError + " - found 'domain reactions' but field 'reaction' was not specified");
      if(!(domainReaction.contains("lookup"))) this->throwParserError(baseError + " - found 'domain reactions' but field 'lookup' was not specified");
      if(!(domainReaction.contains("side"))) this->throwParserError(baseError + " - found 'domain reactions' but field 'side' was not specified");

      // Get the reaction lookup strings
      const std::string reaction = this->trim(domainReaction["reaction"].get<std::string>());
      const std::string lookup = this->trim(domainReaction["lookup"].get<std::string>());

      // Parse the reaction string so we get a list of reactants and products
      std::vector<std::string> reactants;
      std::vector<std::string> products;
      this->parseReactionString(reactants, products, reaction);

      // Domain reactions can contain wildcards -- if the current reaction contains a wildcard
      // we create a list of more reactions.
      const auto reactionSets = this->parseReactionWildcards(reactants, products, domainReaction);

      // Get sides array
      const std::vector<std::string> sides = domainReaction["side"].get<std::vector<std::string>>();

      // Create vector for temporary storage of reactions
      std::vector<CdrPlasmaSurfaceReactionJSON> domainReactionsVec;
      
      // Go through all reactions
      for (const auto& curReaction : reactionSets){
	const std::vector<std::string> curReactants = curReaction.first;
	const std::vector<std::string> curProducts = curReaction.second;

	// Sanctify the reaction -- make sure that all left-hand side and right-hand side species make sense
	this->sanctifySurfaceReaction(curReactants, curProducts, reaction);

	// This is the reaction index for the current index. The reaction we are currently
	// dealing with is put in domainReactionsVec[reactionIndex]
	const int reactionIndex = domainReactionsVec.size();

	// Parse the scaling factor for the electrode surface reaction
	this->parseDomainReactionRate(reactionIndex, domainReaction, sides);
	this->parseDomainReactionScaling(reactionIndex, domainReaction, sides);

	// Make the string-int encoding so we can encode the reaction properly. Then add the reaction to the pile.
	std::list<int> plasmaReactants;
	std::list<int> neutralReactants;
	std::list<int> photonReactants;
	std::list<int> plasmaProducts;
	std::list<int> neutralProducts;
	std::list<int> photonProducts;
	
	this->getReactionSpecies(plasmaReactants,
				 neutralReactants,
				 photonReactants,
				 plasmaProducts,
				 neutralProducts,
				 photonProducts,
				 curReactants,
				 curProducts);

	// Now create the reaction -- note that the surface reactions support both plasma species and photon species on the
	// left hand side of the reaction
	domainReactionsVec.emplace_back(plasmaReactants, photonReactants, plasmaProducts);
      }
      for (std::string curSide : sides){
	// Create an int, Side::LoHiSide pair of dir, side for the m_domainReactions-map
	curSide = this->trim(curSide);
	std::pair<int, Side::LoHiSide> curPair = std::make_pair(m_dirCharToInt.at(curSide.at(0)), m_sideStringToSide.at(curSide.substr(2,2)));

	// Make sure that the dir+side combination has only been included once
	if(m_domainReactions.find(curPair) != m_domainReactions.end()){
	  this->throwParserError(baseError + " - dir+side-pair '" + curSide.substr(2,2) + "' was listed multiple times");
	}

	m_domainReactions.emplace(curPair, domainReactionsVec);
      }
    }
    for(const auto& curDir : m_dirCharToInt){
      for (const auto& curSide : m_sideStringToSide){
	// Make sure that all dir+side combinations are included
	if(m_domainReactions.find(std::make_pair(curDir.second, curSide.second)) == m_domainReactions.end()){
	  this->throwParserError(baseError + " - dir+side-pair '" + curDir.first + "_" + curSide.first + "' is missing.");
	}
      }
    }
  }
}

void CdrPlasmaJSON::parseDomainReactionRate(const int a_reactionIndex, const json& a_reactionJSON, const std::vector<std::string>& a_sides){
  CH_TIME("CdrPlasmaJSON::parseDomainReactionRate()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseDomainReactionRate()" << endl;
  }

  // This is the reaction string -- it exists if we make it to this code (we know this based on tests earlier in the code)
  const std::string reaction = trim(a_reactionJSON["reaction"].get<std::string>());

  // Create a basic error message for error handling
  const std::string baseError = "CdrPlasmaJSON::parseDomainReactionRate for reaction '" + reaction + "' ";

  // We MUST have a field lookup because it determines how we compute the efficiencies for surface reactions
  if(!(a_reactionJSON.contains("lookup"))) this->throwParserError(baseError + "but field 'lookup' was not specified");

  // Get the lookup method
  const std::string lookup = this->trim(a_reactionJSON["lookup"].get<std::string>());

  // Now go through the various rate-computation methods and fill the relevant containers
  if(lookup == "constant"){
    // If using a constant emission rate, we must get the field 'value'
    if(!(a_reactionJSON.contains("value"))) this->throwParserError(baseError + " and got 'constant' lookup but field 'value' is missing");

    const Real rate = a_reactionJSON["value"].get<Real>();

    for (std::string curSide : a_sides){
      // Create an int, Side::LoHiSide pair of dir, side for the m_domainReactionLookup- and m_domainReactionConstants-map
      curSide = this->trim(curSide);
      std::pair<int, Side::LoHiSide> curPair = std::make_pair(m_dirCharToInt.at(curSide.at(0)), m_sideStringToSide.at(curSide.substr(2,2)));

      // Duplicated inputs are not handled here (they are handled in CdrPlasmaJSON::parseDomainReactions() )
      // Add the constant reaction rate to the appropriate container
      m_domainReactionLookup[curPair].emplace(a_reactionIndex, LookupMethod::Constant);
      m_domainReactionConstants[curPair].emplace(a_reactionIndex, rate);
    }
  }
  else {
    this->throwParserError(baseError + "but lookup specification '" + lookup + "' is not supported");
  }
}

void CdrPlasmaJSON::parseDomainReactionScaling(const int a_reactionIndex, const json& a_reactionJSON, const std::vector<std::string>& a_sides){
  CH_TIME("CdrPlasmaJSON::parseDomainReactionScaling()");
  if(m_verbose){
    pout() << "CdrPlasmaJSON::parseDomainReactionScaling()" << endl;
  }

  Real scale = 1.0;
  if(a_reactionJSON.contains("scale")){
     scale = a_reactionJSON["scale"].get<Real>();
  }

  // Create a function = scale everywhere. Extensions to scaling of more generic types of surface
  // reactions can be done by expanding this routine.
  auto func = [scale](const Real E, const RealVect x) -> Real{
     return scale;
  };

  
  for (std::string curSide : a_sides){
    // Create an int, Side::LoHiSide pair of dir, side for the m_domainReactionLookup- and m_domainReactionConstants-map
    curSide = this->trim(curSide);
    std::pair<int, Side::LoHiSide> curPair = std::make_pair(m_dirCharToInt.at(curSide.at(0)), m_sideStringToSide.at(curSide.substr(2,2)));

    // Duplicated inputs are not handled here (they are handled in CdrPlasmaJSON::parseDomainReactions() )
    // Add to the pile
    m_domainReactionEfficiencies[curPair].emplace(a_reactionIndex, func);
  }  
}

int CdrPlasmaJSON::getNumberOfPlotVariables() const {
  int ret = 0;

  if(m_plotGas){
    ret = 3;
  }

  for (const auto& m : m_plasmaReactionPlot){
    if(m.second) ret++;
  }

  if(m_plotAlpha){
    ret++;
  }

  if(m_plotEta){
    ret++;
  }  

  return ret;
}

Vector<std::string> CdrPlasmaJSON::getPlotVariableNames() const {
  Vector<std::string> ret(0);

  if(m_plotGas){
    ret.push_back("gas pressure"      );
    ret.push_back("gas temperature"   );
    ret.push_back("gas number density");
  }

  for (const auto& m : m_plasmaReactionPlot){
    if(m.second){
      ret.push_back(m_plasmaReactionDescriptions.at(m.first));
    }
  }

  if(m_plotAlpha){
    ret.push_back("Townsend ionization coefficient");
  }

  if(m_plotEta){
    ret.push_back("Townsend attachment coefficient");
  }  

  return ret;
}

Vector<Real> CdrPlasmaJSON::getPlotVariables(const Vector<Real>     a_cdrDensities,
					     const Vector<RealVect> a_cdrGradients,
					     const Vector<Real>     a_rteDensities,
					     const RealVect         a_E,
					     const RealVect         a_pos,
					     const Real             a_dx,
					     const Real             a_dt,
					     const Real             a_time,
					     const Real             a_kappa) const {
  Vector<Real> ret(0);

  const std::vector<Real    >& cdrDensities = ((Vector<Real    >&) a_cdrDensities).stdVector();
  const std::vector<RealVect>& cdrGradients = ((Vector<RealVect>&) a_cdrGradients).stdVector();
  const std::vector<Real    >& rteDensities = ((Vector<Real    >&) a_rteDensities).stdVector();

  // These may or may not be needed.
  const std::vector<Real    > cdrMobilities            = this->computePlasmaSpeciesMobilities  (        a_pos, a_E,   cdrDensities)            ;  
  const std::vector<Real    > cdrDiffusionCoefficients = this->computeCdrDiffusionCoefficients (a_time, a_pos, a_E, a_cdrDensities).stdVector();
  const std::vector<Real    > cdrTemperatures          = this->computePlasmaSpeciesTemperatures(        a_pos, a_E,   cdrDensities)            ;

  // Electric field and reduce electric field. 
  const Real E   = a_E.vectorLength();
  const Real N   = m_gasDensity(a_pos);
  const Real Etd = (E/(N * Units::Td));

  // Townsend ionization and attachment coefficients. May or may not be used.
  const Real alpha = this->computeAlpha(E, a_pos);
  const Real eta   = this->computeEta  (E, a_pos);

  // Grid cell volume
  const Real vol = std::pow(a_dx, SpaceDim);  

  // Plot the gas data. 
  if(m_plotGas){
    ret.push_back(m_gasPressure   (a_pos));
    ret.push_back(m_gasTemperature(a_pos));
    ret.push_back(m_gasDensity    (a_pos));
  }

  //  MayDay::Error("CdrPlasmaJSON::getPlotVariables -- need to start from here -- something fishy about the reactions");

  for (const auto& m : m_plasmaReactionPlot){
    if(m.second){
      const int reactionIndex = m.first;
      
      // Compute the reaction rate for the current reaction.
      const Real k = this->computePlasmaReactionRate(reactionIndex,
						     cdrDensities,
						     cdrMobilities,
						     cdrDiffusionCoefficients,
						     cdrTemperatures,
						     cdrGradients,
						     a_pos,
						     a_E,
						     E,
						     Etd,
						     N,
						     alpha,
						     eta,
						     a_time);    


      ret.push_back(k);
    }
  }

  if(m_plotAlpha){
    const Real alpha = this->computeAlpha(E, a_pos);
    
    ret.push_back(alpha);
  }

  if(m_plotEta){
    const Real alpha = this->computeEta(E, a_pos);
    
    ret.push_back(eta);
  }  

  return ret;
}

bool CdrPlasmaJSON::isNeutralSpecies(const std::string& a_name) const {
  bool found = false;

  if(m_neutralSpeciesMap.find(a_name) != m_neutralSpeciesMap.end()){
    found = true;
  }
  else {
    found = false;
  }

  return found;
}

bool CdrPlasmaJSON::isPlasmaSpecies(const std::string& a_name) const {
  bool found = false;

  if(m_cdrSpeciesMap.find(a_name) != m_cdrSpeciesMap.end()){
    found = true;
  }
  else {
    found = false;
  }

  return found;
}

bool CdrPlasmaJSON::isPhotonSpecies(const std::string& a_name) const {
  bool found = false;

  if(m_rteSpeciesMap.find(a_name) != m_rteSpeciesMap.end()){
    found = true;
  }
  else {
    found = false;
  }

  return found;
}

bool CdrPlasmaJSON::doesFileExist(const std::string a_filename) const {
  std::ifstream istream(a_filename);

  bool exists = false;
  if(istream.good()){
    exists = true;
  }

  return exists;
}

std::vector<Real> CdrPlasmaJSON::computePlasmaSpeciesMobilities(const RealVect&          a_position,
								const RealVect&          a_E,
								const std::vector<Real>& a_cdrDensities) const {

  // Get E/N .
  const Real E   = a_E.vectorLength();
  const Real N   = m_gasDensity(a_position);
  const Real Etd = (E/(N * Units::Td));

  // vector of mobilities
  std::vector<Real> mu(m_numCdrSpecies, 0.0);  

  // Go through each species. 
  for (int i = 0; i < a_cdrDensities.size(); i++){
    const bool isMobile = m_cdrSpecies[i]->isMobile();
    const int  Z        = m_cdrSpecies[i]->getChargeNumber();

    // Figure out how to compute the moiblity. 
    if(isMobile && Z != 0){

      const LookupMethod& method = m_mobilityLookup.at(i);
      
      switch(method) {
      case LookupMethod::Constant:
	{
	  mu[i] = m_mobilityConstants.at(i);
	  break;
	}
      case LookupMethod::FunctionEN:
	{
	  mu[i] = m_mobilityFunctionsEN.at(i)(E, N);
	  
	  break;
	}
      case LookupMethod::TableEN:
	{
	  // Recall; the mobility tables are stored as (E/N, mu*N) so we need to extract mu from that. 
	  const LookupTable<2>& mobilityTable = m_mobilityTablesEN.at(i);

	  mu[i]  = mobilityTable.getEntry<1>(Etd); // Get mu*N
	  mu[i] /= N;                              // Get mu

	  break;
	}
      default:
	{
	  MayDay::Error("CdrPlasmaJSON::computePlasmaSpeciesMobilities -- logic bust when computing the mobility. ");
	}
      }
    }
  }

  return mu;
}

std::vector<Real> CdrPlasmaJSON::computePlasmaSpeciesTemperatures(const RealVect&          a_position,
								  const RealVect&          a_E,
								  const std::vector<Real>& a_cdrDensities) const {

  // Electric field and neutral density. 
  const Real N   = m_gasDensity(a_position);
  const Real E   = a_E.vectorLength();
  const Real Etd = (E/(N * Units::Td));

  // Return vector of temperatures. 
  std::vector<Real> T(m_numCdrSpecies, 0.0);
  
  for (int i = 0; i < m_numCdrSpecies; i++){
    const LookupMethod lookup = m_temperatureLookup.at(i);

    // Switch between various lookup methods. 
    switch(lookup) {
    case LookupMethod::FunctionX:
      {
	T[i] = (m_temperatureConstants.at(i))(a_position);
	
	break;
      }
    case LookupMethod::TableEN:
      {
	// Recall; the temperature tables are stored as (E/N, K) so we can fetch the temperature immediately. 
	const LookupTable<2>& temperatureTable = m_temperatureTablesEN.at(i);

	T[i] = temperatureTable.getEntry<1>(Etd);

	break;
      }
    default:
      {
	MayDay::Error("CdrPlasmaJSON::computePlasmaSpeciesTemperatures -- logic bust when computing species temperature");
	
	break;
      }
    }
  }

  return T;
}

Real CdrPlasmaJSON::computePlasmaReactionRate(const int&                   a_reactionIndex,
					      const std::vector<Real>&     a_cdrDensities,					      
					      const std::vector<Real>&     a_cdrMobilities,
					      const std::vector<Real>&     a_cdrDiffusionCoefficients,
					      const std::vector<Real>&     a_cdrTemperatures,					     					      
					      const std::vector<RealVect>& a_cdrGradients,					     
					      const RealVect&              a_pos,					     					     
					      const RealVect&              a_vectorE,
					      const Real&                  a_E,
					      const Real&                  a_Etd,					      
					      const Real&                  a_N,
					      const Real&                  a_alpha,
					      const Real&                  a_eta,					     
					      const Real&                  a_time) const {
  const LookupMethod&          method   = m_plasmaReactionLookup.at(a_reactionIndex);
  const CdrPlasmaReactionJSON& reaction = m_plasmaReactions        [a_reactionIndex];

  // Get list of reactants and products.   
  const std::list<int>& plasmaReactants  = reaction.getPlasmaReactants ();    
  const std::list<int>& neutralReactants = reaction.getNeutralReactants();
  const std::list<int>& plasmaProducts   = reaction.getPlasmaProducts  ();    
  const std::list<int>& photonProducts   = reaction.getPhotonProducts  ();

  // Figure out how to compute the reaction rate. 
  Real k = 0.0;

  // Various ways of computing the reaction rate. 
  switch(method){
  case LookupMethod::Constant:
    {
      k = m_plasmaReactionConstants.at(a_reactionIndex);

      for (const auto& n : neutralReactants){
	k *= (m_neutralSpeciesDensities[n])(a_pos);
      }

      break;
    }
  case LookupMethod::FunctionEN:
    {
      k  = m_plasmaReactionFunctionsEN.at(a_reactionIndex)(a_E, a_N);
      k *= a_N;
	  
      break;
    }
  case LookupMethod::AlphaV:
    {
      const int idx = m_plasmaReactionAlphaV.at(a_reactionIndex);

      k = a_alpha * a_E * a_cdrMobilities[idx];

      break;
    }
  case LookupMethod::EtaV:
    {
      const int idx = m_plasmaReactionEtaV.at(a_reactionIndex);

      k = a_eta * a_E * a_cdrMobilities[idx];

      break;
    }      
  case LookupMethod::FunctionTT:
    {
      const std::tuple<int, int, FunctionTT>& tup = m_plasmaReactionFunctionsTT.at(a_reactionIndex);

      const int         idx1 = std::get<0>(tup);
      const int         idx2 = std::get<1>(tup);
      const FunctionTT& func = std::get<2>(tup);

      const Real T1 = (idx1 < 0) ? m_gasTemperature(a_pos) : a_cdrTemperatures[idx1];
      const Real T2 = (idx2 < 0) ? m_gasTemperature(a_pos) : a_cdrTemperatures[idx2];
	
      k = func(T1, T2);

      for (const auto& n : neutralReactants){
	k *= (m_neutralSpeciesDensities[n])(a_pos);
      }

      break;
    }
  case LookupMethod::TableEN:
    {
      // Recall; the reaction tables are stored as (E/N, rate/N) so we need to extract mu from that. 
      const LookupTable<2>& reactionTable = m_plasmaReactionTablesEN.at(a_reactionIndex);

      // Get the reaction rate. 
      k  = reactionTable.getEntry<1>(a_Etd);

      // Multiply by neutral species densities. 
      for (const auto& n : neutralReactants){
	k *= (m_neutralSpeciesDensities[n])(a_pos);
      }
	  
      break;
    }
  default:
    {
      MayDay::Error("CdrPlasmaJSON::computePlasmaReactionRate -- logic bust");
      
      break;
    }
  }

  // Modify by other parameters.
  k *= m_plasmaReactionEfficiencies.at(a_reactionIndex)(a_E, a_pos);

  // This is a hook that uses the Soloviev correction. It modifies the reaction rate according to k = k * (1 + (E.D*grad(n))/(K * n * E^2) where
  // K is the electron mobility. 
  if( (m_plasmaReactionSolovievCorrection.at(a_reactionIndex)).first){
    const int species = (m_plasmaReactionSolovievCorrection.at(a_reactionIndex)).second;

    const Real&     n  = a_cdrDensities            [species];
    const Real&     mu = a_cdrMobilities           [species];
    const Real&     D  = a_cdrDiffusionCoefficients[species];
    const RealVect& g  = a_cdrGradients            [species];

    // Compute correction factor 1 + E.(D*grad(n))/(K * n * E^2). 
    constexpr Real safety = 1.0;
      
    Real fcorr  = 1.0 + (a_vectorE.dotProduct(D*g)) / ( safety + n * mu * a_E * a_E);

    fcorr = std::max(fcorr, 0.0);
      
    k *= fcorr;
  }

  // Finally compute the total consumption. After this, k -> total consumption. 
  for (const auto& r : plasmaReactants){
    k *= a_cdrDensities[r];
  }

  return k;
}

Real CdrPlasmaJSON::computeAlpha(const Real a_E, const RealVect a_position) const {
  Real alpha = 0.0;

  const Real N   = m_gasDensity(a_position);
  const Real Etd = a_E/(Units::Td * N);

  switch(m_alphaLookup) {
  case LookupMethod::TableEN:
    {
      alpha  = m_alphaTableEN.getEntry<1>(Etd); // Get alpha/N
      alpha *= N;                               // Get alpha

      break;
    }
  default:
    {
      MayDay::Error("CdrPlasmaJSON::computeAlpha -- logic bust");
      
      break;
    }
  }

  return alpha;
}

Real CdrPlasmaJSON::computeEta(const Real a_E, const RealVect a_position) const {
  Real eta = 0.0;

  const Real N   = m_gasDensity(a_position);
  const Real Etd = a_E/(Units::Td * N);

  switch(m_etaLookup) {
  case LookupMethod::TableEN:
    {
      eta  = m_etaTableEN.getEntry<1>(Etd); // Get eta/N
      eta *= N;                             // Get eta

      break;
    }
  default:
    {
      MayDay::Error("CdrPlasmaJSON::computeEta -- logic bust");
      
      break;
    }
  }

  return eta;  
} 

void CdrPlasmaJSON::advanceReactionNetwork(Vector<Real>&          a_cdrSources,
					   Vector<Real>&          a_rteSources,
					   const Vector<Real>     a_cdrDensities,
					   const Vector<RealVect> a_cdrGradients,
					   const Vector<Real>     a_rteDensities,
					   const RealVect         a_E,
					   const RealVect         a_pos,
					   const Real             a_dx,
					   const Real             a_dt,
					   const Real             a_time,
					   const Real             a_kappa) const {
  // I really hate Chombo sometimes. 
  std::vector<Real>& cdrSources = a_cdrSources.stdVector();
  std::vector<Real>& rteSources = a_rteSources.stdVector();

  const std::vector<Real    >& cdrDensities = ((Vector<Real    >&) a_cdrDensities).stdVector();
  const std::vector<Real    >& rteDensities = ((Vector<Real    >&) a_rteDensities).stdVector();
  const std::vector<RealVect>& cdrGradients = ((Vector<RealVect>&) a_cdrGradients).stdVector();  

  // These may or may not be needed.
  const std::vector<Real    > cdrMobilities            = this->computePlasmaSpeciesMobilities  (        a_pos, a_E,   cdrDensities)            ;  
  const std::vector<Real    > cdrDiffusionCoefficients = this->computeCdrDiffusionCoefficients (a_time, a_pos, a_E, a_cdrDensities).stdVector();
  const std::vector<Real    > cdrTemperatures          = this->computePlasmaSpeciesTemperatures(        a_pos, a_E,   cdrDensities)            ;

  // Electric field and reduce electric field. 
  const Real E   = a_E.vectorLength();
  const Real N   = m_gasDensity(a_pos);
  const Real Etd = (E/(N * Units::Td));

  // Townsend ionization and attachment coefficients. May or may not be used.
  const Real alpha = this->computeAlpha(E, a_pos);
  const Real eta   = this->computeEta  (E, a_pos);

  // Grid cell volume
  const Real vol = std::pow(a_dx, SpaceDim);

  // Set all sources to zero. 
  for (auto& S : cdrSources){
    S = 0.0;
  }

  for (auto& S : rteSources){
    S = 0.0;
  }

  // Hook for turning off all reactions. 
  if(!m_skipReactions){

    // Plasma reactions loop
    for (int i = 0; i < m_plasmaReactions.size(); i++) {

      // Reaction and species involved in the reaction. The lambda above does *not* multiply by neutral species densities. Since rates
      // can be parsed in so many different ways, the rules for the various rates are put in the switch statement below.
      const CdrPlasmaReactionJSON& reaction  = m_plasmaReactions[i];

      const std::list<int>& plasmaReactants  = reaction.getPlasmaReactants ();    
      const std::list<int>& neutralReactants = reaction.getNeutralReactants();
      const std::list<int>& plasmaProducts   = reaction.getPlasmaProducts  ();    
      const std::list<int>& photonProducts   = reaction.getPhotonProducts  ();

      // Compute the rate. This returns a volumetric rate in units of #/m^(-3) (or #/m^-2 for Cartesian 2D).
      const Real k = this->computePlasmaReactionRate(i,
						     cdrDensities,
						     cdrMobilities,
						     cdrDiffusionCoefficients,
						     cdrTemperatures,
						     cdrGradients,
						     a_pos,
						     a_E,
						     E,
						     Etd,
						     N,
						     alpha,
						     eta,
						     a_time);    

      // Remove consumption on the left-hand side.
      for (const auto& r : plasmaReactants){
	cdrSources[r] -= k;
      }

      // Add mass on the right-hand side.
      for (const auto& p : plasmaProducts){
	cdrSources[p] += k;
      }

      for (const auto& p : photonProducts){
	rteSources[p] += k;
      }    
    } // End of plasma reactions. 

    // Photo-reactions loop.
    for (int i = 0; i < m_photoReactions.size(); i++){
      Real k = 0.0;

      const CdrPlasmaPhotoReactionJSON& reaction = m_photoReactions[i];

      // Get the photon and plasma products for the specified reaction. 
      const std::list<int>& photonReactants = reaction.getPhotonReactants();
      const std::list<int>& plasmaProducts  = reaction.getPlasmaProducts ();    


      // Compute a rate. Note that if we use Helmholtz reconstruction this is a bit different. 
      if(m_photoReactionUseHelmholtz.at(i)){
	k = m_photoReactionEfficiencies.at(i)(E, a_pos);
      }
      else{
	// This is the "regular" code where Psi is the number of ionizing photons. In this case the Psi is what appears in the source terms,
	// but the API says that we need to compute the rate, so we put rate = Psi/dt
      
	k = m_photoReactionEfficiencies.at(i)(E, a_pos)/a_dt;

	// Hook for ensuring correct scaling in 2D. When we run in Cartesian 2D the photons are not points, but lines. We've deposited the particles (lines)
	// on the mesh but what we really want is the volumetric density. So we need to scale. 
	if(m_discretePhotons && SpaceDim == 2){
	  k *= 1./a_dx;
	}
      }

      // Fire the reaction. 
      for (const auto& y : photonReactants){
	k *= rteDensities[y];
      }

      for (const auto& p : plasmaProducts){
	cdrSources[p] += k;
      }
    } // End of photo-reactions. 

    // If using stochastic photons -- then we need to run Poisson sampling of the photons.
    if(m_discretePhotons){
      for (auto& S : rteSources){
	const auto poissonSample = Random::getPoisson<unsigned long long>(S * vol * a_dt);
      
	S = Real(poissonSample);
      }
    }
  }

  return;
}

Vector<RealVect> CdrPlasmaJSON::computeCdrDriftVelocities(const Real         a_time,
							  const RealVect     a_position,
							  const RealVect     a_E,
							  const Vector<Real> a_cdrDensities) const {

  // I really hate Chombo sometimes.
  const std::vector<Real>& cdrDensities = ((Vector<Real>&) a_cdrDensities).stdVector();  

  // Compute the mobilities for each species. 
  const std::vector<Real> mu = this->computePlasmaSpeciesMobilities(a_position, a_E, cdrDensities);

  // Return vector
  Vector<RealVect> velocities(m_numCdrSpecies, RealVect::Zero);

  // Make sure v = +/- mu*E depending on the sign charge. 
  for (int i = 0; i < a_cdrDensities.size(); i++){
    const int Z = m_cdrSpecies[i]->getChargeNumber();

    if(Z > 0){
      velocities[i] = + mu[i] * a_E;
    }
    else if(Z < 0){
      velocities[i] = - mu[i] * a_E;
    }
  }

  return velocities;
}

Vector<Real> CdrPlasmaJSON::computeCdrDiffusionCoefficients(const Real         a_time,
							    const RealVect     a_position,
							    const RealVect     a_E,
							    const Vector<Real> a_cdrDensities) const {
  Vector<Real> diffusionCoefficients(m_numCdrSpecies, 0.0);

  const Real E   = a_E.vectorLength();
  const Real N   = m_gasDensity(a_position);
  const Real Etd = (E/(N * Units::Td));    

  for (int i = 0; i < a_cdrDensities.size(); i++){
    if(m_cdrSpecies[i]->isDiffusive()){
      
      // Figure out how we compute the diffusion coefficient for this species. 
      const LookupMethod& method = m_diffusionLookup.at(i);

      Real Dco = 0.0;
      
      switch(method) {
      case LookupMethod::Constant:
	{
	  Dco = m_diffusionConstants.at(i);
	  
	  break;
	}
      case LookupMethod::FunctionEN:
	{
	  const Real E = a_E.vectorLength();	  
	  const Real N = m_gasDensity(a_position);

	  Dco = m_diffusionFunctionsEN.at(i)(E, N);

	  break;
	}
      case LookupMethod::TableEN:
	{
	  // Recall; the diffusion tables are stored as (E/N, D*N) so we need to extract D from that. 
	  const LookupTable<2>& diffusionTable = m_diffusionTablesEN.at(i);

	  Dco  = diffusionTable.getEntry<1>(Etd); // Get D*N
	  Dco /= N;                               // Get D

	  break;
	}
      default:
	{
	  MayDay::Error("CdrPlasmaJSON::computeCdrDiffusionCoefficients -- logic bust");
	}
      }

      diffusionCoefficients[i] = Dco;
    }
  }

  return diffusionCoefficients;  
}

Vector<Real> CdrPlasmaJSON::computeCdrElectrodeFluxes(const Real         a_time,
						      const RealVect     a_pos,
						      const RealVect     a_normal,
						      const RealVect     a_E,
						      const Vector<Real> a_cdrDensities,
						      const Vector<Real> a_cdrVelocities,
						      const Vector<Real> a_cdrGradients,
						      const Vector<Real> a_rteFluxes,
						      const Vector<Real> a_extrapCdrFluxes) const {
  
  // TLDR: This routine computes the finite volume fluxes on the EB. The input argument a_extrapCdrFluxes are the fluxes
  //       that were extrapolated from the inside of the domain. Likewise, a_rteFluxes are the photon fluxes onto the surfaces. We
  //       use these fluxes to specify an inflow due to secondary emission. 

  // Storage for "natural" outflow fluxes, and inflow fluxes due to secondary emission. 
  std::vector<Real> outflowFluxes(m_numCdrSpecies, 0.0);
  std::vector<Real> inflowFluxes (m_numCdrSpecies, 0.0);

  // Check if this is an anode or a cathode. 
  const bool isCathode = a_E.dotProduct(a_normal) < 0;
  const bool isAnode   = a_E.dotProduct(a_normal) > 0;

  // Compute the magnitude of the electric field both in SI units and Townsend units
  const Real N   = m_gasDensity(a_pos);
  const Real E   = a_E.vectorLength();
  const Real Etd = E/(Units::Td * N);

  // Compute the outflow fluxes. 
  for (int i = 0; i < m_numCdrSpecies; i++){
    const int Z = m_cdrSpecies[i]->getChargeNumber();

    // Outflow on of negative species on anodes. 
    if(Z < 0 && isAnode  ) outflowFluxes[i] = std::max(0.0, a_extrapCdrFluxes[i]);
    if(Z > 0 && isCathode) outflowFluxes[i] = std::max(0.0, a_extrapCdrFluxes[i]);
  }

  // Go through our list of electrode reactions and compute the inflow fluxes from secondary emission from plasma species
  // and photon species. 
  for (int i = 0; i < m_electrodeReactions.size(); i++){

    // Get the reaction and lookup method. 
    const LookupMethod&                 method   = m_electrodeReactionLookup.at(i);
    const CdrPlasmaSurfaceReactionJSON& reaction = m_electrodeReactions        [i];

    // Get the outgoing species that are involved in the reaction.
    const std::list<int>& plasmaReactants = reaction.getPlasmaReactants();
    const std::list<int>& photonReactants = reaction.getPhotonReactants();
    const std::list<int>& plasmaProducts  = reaction.getPlasmaProducts ();    

    // Get the emission rate constant. 
    Real emissionRate = 0.0;
    switch(method){
    case LookupMethod::Constant:
      {
	emissionRate = m_electrodeReactionConstants.at(i);

	break;
      }
    default:
      {
	MayDay::Error("CdrPlasmaJSON::computeCdrElectrodeFluxes -- logic bust");
      }
    }

    // Scale the emission rate constant by whatever the user has put in the "scaling factor" field.
    const FunctionEX& scalingFunction = m_electrodeReactionEfficiencies.at(i);
    const Real        scalingFactor   = scalingFunction(E, a_pos);

    emissionRate *= scalingFactor;

    // Next, compute the total influx due to outflow of the specified surface reaction species. 
    Real inflow = 0.0;

    // Inflow due to outflow of specified plasma species
    for (const auto& r : plasmaReactants){
      inflow += outflowFluxes[r];
    }

    // Inflow due to outflow of specified photon flux
    for (const auto& r : photonReactants){
      inflow += a_rteFluxes[r];
    }

    // Scale by emission rate in order to get total influx. 
    inflow *= emissionRate;

    // Add the inflow flux to all species on the right-hand side of the reaction.
    for (const auto& p : plasmaProducts){
      inflowFluxes[p] += inflow;
    }
  }

  // Now set the finite volume fluxes on the EB accordingly. The negative sign is because 'inflowFluxes' is the magnitude, but in our
  // finite-volume implementation a mass inflow into the cut-cell will have a negative sign.
  Vector<Real> fluxes(m_numCdrSpecies, 0.0);
  
  for (int i = 0; i < m_numCdrSpecies; i++){
    fluxes[i] = outflowFluxes[i] - inflowFluxes[i];
  }
  
  return fluxes;
}

Vector<Real> CdrPlasmaJSON::computeCdrDielectricFluxes(const Real         a_time,
						       const RealVect     a_pos,
						       const RealVect     a_normal,
						       const RealVect     a_E,
						       const Vector<Real> a_cdrDensities,
						       const Vector<Real> a_cdrVelocities,
						       const Vector<Real> a_cdrGradients,
						       const Vector<Real> a_rteFluxes,
						       const Vector<Real> a_extrapCdrFluxes) const {

  // TLDR: This routine computes the finite volume fluxes on the EB. The input argument a_extrapCdrFluxes are the fluxes
  //       that were extrapolated from the inside of the domain. Likewise, a_rteFluxes are the photon fluxes onto the surfaces. We
  //       use these fluxes to specify an inflow due to secondary emission. 

  // Storage for "natural" outflow fluxes, and inflow fluxes due to secondary emission. 
  std::vector<Real> outflowFluxes(m_numCdrSpecies, 0.0);
  std::vector<Real> inflowFluxes (m_numCdrSpecies, 0.0);

  // Check if this is an anode or a cathode. 
  const bool isCathode = a_E.dotProduct(a_normal) < 0;
  const bool isAnode   = a_E.dotProduct(a_normal) > 0;

  // Compute the magnitude of the electric field both in SI units and Townsend units
  const Real N   = m_gasDensity(a_pos);
  const Real E   = a_E.vectorLength();
  const Real Etd = E/(Units::Td * N);

  // Compute the outflow fluxes. 
  for (int i = 0; i < m_numCdrSpecies; i++){
    const int Z = m_cdrSpecies[i]->getChargeNumber();

    // Outflow on of negative species on anodes. 
    if(Z < 0 && isAnode  ) outflowFluxes[i] = std::max(0.0, a_extrapCdrFluxes[i]);
    if(Z > 0 && isCathode) outflowFluxes[i] = std::max(0.0, a_extrapCdrFluxes[i]);
  }

  // Go through our list of dielectric reactions and compute the inflow fluxes from secondary emission from plasma species
  // and photon species. 
  for (int i = 0; i < m_dielectricReactions.size(); i++){

    // Get the reaction and lookup method. 
    const LookupMethod&                 method   = m_dielectricReactionLookup.at(i);
    const CdrPlasmaSurfaceReactionJSON& reaction = m_dielectricReactions        [i];

    // Get the outgoing species that are involved in the reaction.
    const std::list<int>& plasmaReactants = reaction.getPlasmaReactants();
    const std::list<int>& photonReactants = reaction.getPhotonReactants();
    const std::list<int>& plasmaProducts  = reaction.getPlasmaProducts ();    

    // Get the emission rate constant. 
    Real emissionRate = 0.0;
    switch(method){
    case LookupMethod::Constant:
      {
	emissionRate = m_dielectricReactionConstants.at(i);

	break;
      }
    default:
      {
	MayDay::Error("CdrPlasmaJSON::computeCdrDielectricFluxes -- logic bust");
      }
    }

    // Scale the emission rate constant by whatever the user has put in the "scaling factor" field.
    const FunctionEX& scalingFunction = m_dielectricReactionEfficiencies.at(i);
    const Real        scalingFactor   = scalingFunction(E, a_pos);

    emissionRate *= scalingFactor;

    // Next, compute the total influx due to outflow of the specified surface reaction species. 
    Real inflow = 0.0;

    // Inflow due to outflow of specified plasma species
    for (const auto& r : plasmaReactants){
      inflow += outflowFluxes[r];
    }

    // Inflow due to outflow of specified photon flux
    for (const auto& r : photonReactants){
      inflow += a_rteFluxes[r];
    }

    // Scale by emission rate in order to get total influx. 
    inflow *= emissionRate;

    // Add the inflow flux to all species on the right-hand side of the reaction.
    for (const auto& p : plasmaProducts){
      inflowFluxes[p] += inflow;
    }
  }

  // Now set the finite volume fluxes on the EB accordingly. The negative sign is because 'inflowFluxes' is the magnitude, but in our
  // finite-volume implementation a mass inflow into the cut-cell will have a negative sign.
  Vector<Real> fluxes(m_numCdrSpecies, 0.0);
  
  for (int i = 0; i < m_numCdrSpecies; i++){
    fluxes[i] = outflowFluxes[i] - inflowFluxes[i];
  }
  
  return fluxes;  
}

Vector<Real> CdrPlasmaJSON::computeCdrDomainFluxes(const Real           a_time,
						   const RealVect       a_pos,
						   const int            a_dir,
						   const Side::LoHiSide a_side,
						   const RealVect       a_E,
						   const Vector<Real>   a_cdrDensities,
						   const Vector<Real>   a_cdrVelocities,
						   const Vector<Real>   a_cdrGradients,
						   const Vector<Real>   a_rteFluxes,
						   const Vector<Real>   a_extrapCdrFluxes) const {
  
  // TLDR: This routine computes the finite volume fluxes on the domain. The input argument a_extrapCdrFluxes are the fluxes
  //       that were extrapolated from the inside of the domain. Likewise, a_rteFluxes are the photon fluxes onto the surfaces. We
  //       use these fluxes to specify an inflow due to secondary emission. 
  
  // The finite volume fluxes. In our finite-volume implementation, a mass inflow will have a negative sign.
  Vector<Real> fluxes(m_numCdrSpecies, 0.0);  

  const int sign        = (a_side == Side::Lo) ? 1 : -1;
  const RealVect normal = sign * BASISREALV(a_dir);
  const std::pair<int, Side::LoHiSide> dirSide = std::make_pair(a_dir, a_side);

  // Check if this is an anode or a cathode
  const bool isCathode = a_E.dotProduct(normal) < 0;
  const bool isAnode   = a_E.dotProduct(normal) > 0;

  // Compute the magnitude of the electric field in SI units
  const Real E = a_E.vectorLength();

  // Set outflow boundary conditions on charged species. 
  for (int i = 0; i < m_numCdrSpecies; i++){
    const int Z = m_cdrSpecies[i]->getChargeNumber();

    // Outflow on of negative species on anodes. 
    if(Z < 0 && isAnode  ) fluxes[i] = std::max(0.0, a_extrapCdrFluxes[i]);
    if(Z > 0 && isCathode) fluxes[i] = std::max(0.0, a_extrapCdrFluxes[i]);      
  }

  // Go through our list of dielectric reactions and compute the inflow fluxes from secondary emission from plasma species
  // and photon species
  for (int i = 0; i < m_domainReactions.at(dirSide).size(); ++i){

    // Get the reaction and lookup method.
    const LookupMethod& method = m_domainReactionLookup.at(dirSide).at(i);
    const CdrPlasmaSurfaceReactionJSON& reaction = m_domainReactions.at(dirSide)[i];

    // Get the outgoing species that are involved in the reaction
    const std::list<int>& plasmaReactants = reaction.getPlasmaReactants();
    const std::list<int>& photonReactants = reaction.getPhotonReactants();
    const std::list<int>& plasmaProducts = reaction.getPlasmaProducts();

    // Get the emission rate constant
    Real emissionRate = 0.0;
    switch(method){
    case LookupMethod::Constant:
      {
	emissionRate = m_domainReactionConstants.at(dirSide).at(i);
	break;
      }
    default:
      {
	MayDay::Error("CdrPlasmaJSON::computeCdrDomainFluxes -- logic bust");
      }
    }

    // Scale the emission rate constant by whatever the user has put in the "scaling factor" field.
    const FunctionEX& scalingFunction = m_domainReactionEfficiencies.at(dirSide).at(i);
    const Real scalingFactor = scalingFunction(E, a_pos);

    emissionRate *= scalingFactor;

    // Next, compute the total influx due to outflow of the specified surface reaction species.
    Real inflow = 0.0;

    // Inflow due to outflow of specified plasma species
    for (const auto& r : plasmaReactants){
      inflow += fluxes[r];
    }

    // Inflow due to outflow of specified photon flux
    for (const auto& r : photonReactants){
      inflow += a_extrapCdrFluxes[r];
    }

    // Scale by emission rate in order to get total influx
    inflow *= emissionRate;

    // Add the inflow flux to all species on the right-hand-side of the reaction, i.e. subtract it.
    // It is subtracted because 'inflowFluxes' is the magnitude, but in our
    // finite-volume implementation a mass inflow into the cut-cell will have a negative sign.
    for (const auto& p : plasmaProducts){
      fluxes[p] -= inflow;
    }
  }
  
  return fluxes;    
}

Real CdrPlasmaJSON::initialSigma(const Real a_time, const RealVect a_pos) const {
  return m_initialSigma(a_pos, a_time);
}

#include <CD_NamespaceFooter.H>

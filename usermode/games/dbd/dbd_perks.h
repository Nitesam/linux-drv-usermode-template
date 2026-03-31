#ifndef DBD_PERKS_H
#define DBD_PERKS_H

#include <string>
#include <string_view>
#include <unordered_map>

inline const std::unordered_map<std::string_view, std::string_view> g_Perks = {
    // Survivor - General
    {"Dark_Sense", "Dark Sense"},
    {"Deja_Vu", "Déjà Vu"},
    {"Hope", "Hope"},
    {"Kindred", "Kindred"},
    {"Lightweight", "Lightweight"},
    {"No_One_Left_Behind", "No One Left Behind"},
    {"Plunderers_Instinct", "Plunderer's Instinct"},
    {"Premonition", "Premonition"},
    {"Resilience", "Resilience"},
    {"Slippery_Meat", "Slippery Meat"},
    {"Small_Game", "Small Game"},
    {"Spine_Chill", "Spine Chill"},
    {"This_Is_Not_Happening", "This Is Not Happening"},
    {"WellMakeIt", "We'll Make It"},

    // Dwight
    {"Leader", "Leader"},
    {"Prove_Thyself", "Prove Thyself"},
    {"Bond", "Bond"},

    // Meg
    {"Sprint_Burst", "Sprint Burst"},
    {"QuickQuiet", "Quick & Quiet"},
    {"Adrenaline", "Adrenaline"},

    // Claudette
    {"Self_Care", "Self-care"},
    {"Empathy", "Empathy"},
    {"Botany_Knowledge", "Botany Knowledge"},

    // Jake
    {"Sabotuer", "Saboteur"},
    {"Iron_Will", "Iron Will"},
    {"Calm_Spirit", "Calm Spirit"},

    // Nea
    {"Urban_Evasion", "Urban Evasion"},
    {"Streetwise", "Streetwise"},
    {"Balanced_Landing", "Balanced Landing"},

    // Bill
    {"SelfSufficient", "Unbreakable"},
    {"LeftBehind", "Left Behind"},
    {"BorrowedTime", "Borrowed Time"},

    // Laurie
    {"SoleSurvivor", "Sole Survivor"},
    {"ObjectOfObsession", "Object of Obsession"},
    {"DecisiveStrike", "Decisive Strike"},

    // Ace
    {"Up_The_Ante", "Up the Ante"},
    {"Open_Handed", "Open-handed"},
    {"Ace_In_The_Hole", "Ace in the Hole"},

    // Feng Min
    {"Technician", "Technician"},
    {"Lithe", "Lithe"},
    {"Alert", "Alert"},

    // David
    {"WereGonnaLiveForever", "We're Gonna Live Forever"},
    {"NoMither", "No Mither"},
    {"DeadHard", "Dead Hard"},

    // Quentin
    {"Wakeup", "Wake Up!"},
    {"Pharmacy", "Pharmacy"},
    {"Vigil", "Vigil"},

    // Tapp
    {"Tenacity", "Tenacity"},
    {"StakeOut", "Stake Out"},
    {"DetectivesHunch", "Detective's Hunch"},

    // Kate
    {"WindowsOfOpportunity", "Windows of Opportunity"},
    {"Dance_with_me", "Dance With Me"},
    {"BoilOver", "Boil Over"},

    // Adam
    {"Diversion", "Diversion"},
    {"Deliverance", "Deliverance"},
    {"Autodidact", "Autodidact"},

    // Jeff
    {"Distortion", "Distortion"},
    {"Breakdown", "Breakdown"},
    {"AfterCare", "Aftercare"},

    // Jane
    {"Solidarity", "Solidarity"},
    {"Poised", "Poised"},
    {"HeadOn", "Head On"},

    // Ashley
    {"TheMettleOfMan", "Mettle of Man"},
    {"FlipFlop", "Flip-flop"},
    {"BuckleUp", "Buckle Up"},

    // Nancy
    {"InnerStrength", "Inner Strength"},
    {"Fixated", "Fixated"},
    {"BetterTogether", "Better Together"},

    // Steve
    {"SecondWind", "Second Wind"},
    {"Camaraderie", "Camaraderie"},
    {"Babysitter", "Babysitter"},

    // Yui
    {"LuckyBreak", "Lucky Break"},
    {"Breakout", "Breakout"},
    {"AnyMeansNecessary", "Any Means Necessary"},

    // Zarina
    {"RedHerring", "Red Herring"},
    {"OffTheRecord", "Off the Record"},
    {"ForThePeople", "For the People"},

    // Cheryl
    {"SoulGuard", "Soul Guard"},
    {"RepressedAlliance", "Repressed Alliance"},
    {"BloodPact", "Blood Pact"},

    // Felix
    {"Visionary", "Visionary"},
    {"DesperateMeasures", "Desperate Measures"},
    {"BuiltToLast", "Built to Last"},

    // Elodie
    {"S24P03", "Power Struggle"},
    {"S24P02", "Deception"},
    {"S24P01", "Appraisal"},

    // Yun-Jin
    {"S25P02", "Smash Hit"},
    {"S25P03", "Self-preservation"},
    {"S25P01", "Fast Track"},

    // Jill
    {"S26P02", "Resurgence"},
    {"S26P01", "Counterforce"},
    {"S26P03", "Blast Mine"},

    // Leon
    {"S27P03", "Rookie Spirit"},
    {"S27P02", "Flashbang"},
    {"S27P01", "Bite the Bullet"},

    // Mikaela
    {"S28P01", "Clairvoyance"},
    {"S28P03", "Boon: Shadow Step"},
    {"S28P02", "Boon: Circle of Healing"},

    // Jonah
    {"S29P01", "Overcome"},
    {"S29P02", "Corrective Action"},
    {"S29P03", "Boon: Exponential"},

    // Yoichi
    {"S30P01", "Parental Guidance"},
    {"S30P02", "Empathic Connection"},
    {"S30P03", "Boon: Dark Theory"},

    // Haddie
    {"S31P02", "Residual Manifest"},
    {"S31P03", "Overzealous"},
    {"S31P01", "Inner Focus"},

    // Ada
    {"S32P01", "Wiretap"},
    {"S32P02", "Reactive Healing"},
    {"S32P03", "Low Profile"},

    // Rebecca
    {"S33P02", "Reassurance"},
    {"S33P03", "Hyperfocus"},
    {"S33P01", "Better Than New"},

    // Vittorio
    {"S34P03", "Quick Gambit"},
    {"S34P01", "Potential Energy"},
    {"S34P02", "Fogwise"},

    // Thalita
    {"S35P03", "Teamwork: Power of Two"},
    {"S35P02", "Friendly Competition"},
    {"S35P01", "Cut Loose"},

    // Renato
    {"S36P03", "Teamwork: Collective Stealth"},
    {"S36P02", "Blood Rush"},
    {"S36P01", "Background Player"},

    // Gabriel
    {"S37P01", "Troubleshooter"},
    {"S37P03", "Scavenger"},
    {"S37P02", "Made For This"},

    // Nicolas
    {"S38P03", "Plot Twist"},
    {"S38P02", "Scene Partner"},
    {"S38P01", "Dramaturgy"},

    // Ellen
    {"S39P01", "Lucky Star"},
    {"S39P03", "Light-footed"},
    {"S39P02", "Chemical Trap"},

    // Alan
    {"S40P03", "Deadline"},
    {"S40P01", "Champion of Light"},
    {"S40P02", "Boon: Illumination"},

    // Sable
    {"S41P03", "Wicked"},
    {"S41P02", "Strength in Shadows"},
    {"S41P01", "Invocation: Weaving Spiders"},

    // Aestri
    {"S42P03", "Still Sight"},
    {"S42P01", "Mirrored Illusion"},
    {"S42P02", "Bardic Inspiration"},

    // Lara
    {"S43P03", "Specialist"},
    {"S43P02", "Hardened"},
    {"S43P01", "Finesse"},

    // Trevor
    {"S44P03", "Moment of Glory"},
    {"S44P01", "Eyes of Belmont"},
    {"S44P02", "Exultation"},

    // Taurie
    {"S45P03", "Shoulder the Burden"},
    {"S45P01", "Invocation: Treacherous Crows"},
    {"S45P02", "Clean Break"},

    // Orela
    {"S46P03", "Rapid Response"},
    {"S46P02", "Duty of Care"},
    {"S46P01", "Do No Harm"},

    // Rick
    {"S47P03", "Teamwork: Toughen Up"},
    {"S47P02", "Come and Get Me!"},
    {"S47P01", "Apocalyptic Ingenuity"},

    // Michonne
    {"S48P03", "Teamwork: Throw Down"},
    {"S48P02", "Last Stand"},
    {"S48P01", "Conviction"},

    // Vee
    {"S49P01", "Road Life"},
    {"S49P02", "One-Two-Three-Four!"},
    {"S49P03", "Ghost Notes"},

    // Dustin
    {"S50P03", "Teamwork: Full Circuit"},
    {"S50P02", "Change of Plan"},
    {"S50P01", "Bada Bada Boom"},

    // Eleven
    {"S51P02", "We See You"},
    {"S51P03", "Teamwork: Soft-spoken"},
    {"S51P01", "Extrasensory Perception"},

    // Kwon
    {"S52P01", "Flow State"},
    {"S52P02", "A Place For Us"},
    {"S52P03", "Five Moves Ahead"},

    // Killer - General
    {"Bitter_Murmur", "Bitter Murmur"},
    {"Deerstalker", "Deerstalker"},
    {"Distressing", "Distressing"},
    {"No_One_Escapes_Death", "Hex: No One Escapes Death"},
    {"Hex_Thrill_Of_The_Hunt", "Hex: Thrill of the Hunt"},
    {"Insidious", "Insidious"},
    {"Iron_Grasp", "Iron Grasp"},
    {"Monstrous_Shrine", "Scourge Hook: Monstrous Shrine"},
    {"BoonDestroyer", "Shattered Hope"},
    {"Sloppy_Butcher", "Sloppy Butcher"},
    {"Spies_From_The_Shadows", "Spies from the Shadows"},
    {"Whispers", "Whispers"},
    {"Unrelenting", "Unrelenting"},

    // Trapper
    {"Agitation", "Agitation"},
    {"Brutal_Strength", "Brutal Strength"},
    {"Unnerving_Presence", "Unnerving Presence"},

    // Wraith
    {"Bloodhound", "Bloodhound"},
    {"Predator", "Predator"},
    {"Shadowborn", "Shadowborn"},

    // Hillbilly
    {"Lightborn", "Lightborn"},
    {"Enduring", "Enduring"},
    {"Tinkerer", "Tinkerer"},

    // Nurse
    {"Thanatophobia", "Thanatophobia"},
    {"Stridor", "Stridor"},
    {"NurseCalling", "A Nurse's Calling"},

    // Shape
    {"Save_The_Best_For_Last", "Save the Best for Last"},
    {"Play_With_Your_Food", "Play With Your Food"},
    {"Dying_Light", "Dying Light"},

    // Hag
    {"Hex_The_Third_Seal", "Hex: The Third Seal"},
    {"Hex_Ruin", "Hex: Ruin"},
    {"Hex_Devour_Hope", "Hex: Devour Hope"},

    // Doctor
    {"OverwhelmingPresence", "Overwhelming Presence"},
    {"GeneratorOvercharge", "Overcharge"},
    {"MonitorAndAbuse", "Monitor & Abuse"},

    // Cannibal
    {"InTheDark", "Knock Out"},
    {"FranklinsLoss", "Franklin's Demise"},
    {"BBQAndChilli", "Barbecue & Chili"},

    // Huntress
    {"TerritorialImperative", "Territorial Imperative"},
    {"BeastOfPrey", "Beast of Prey"},
    {"Hex_HuntressLullaby", "Hex: Huntress Lullaby"},

    // Nightmare
    {"RememberMe", "Remember Me"},
    {"FireUp", "Fire Up"},
    {"BloodWarden", "Blood Warden"},

    // Pig
    {"Surveillance", "Surveillance"},
    {"HangmansTrick", "Scourge Hook: Hangman's Trick"},
    {"MakeYourChoice", "Make Your Choice"},

    // Clown
    {"pop_goes_the_weasel", "Pop Goes The Weasel"},
    {"Bamboozle", "Bamboozle"},
    {"Coulrophobia", "Coulrophobia"},

    // Spirit
    {"Rancor", "Rancor"},
    {"Hex_HauntedGround", "Hex: Haunted Ground"},
    {"SpiritFury", "Spirit Fury"},

    // Legion
    {"Madgrit", "Mad Grit"},
    {"Ironmaiden", "Iron Maiden"},
    {"Discordance", "Discordance"},

    // Plague
    {"InfectiousFright", "Infectious Fright"},
    {"DarkDevotion", "Dark Devotion"},
    {"CorruptIntervention", "Corrupt Intervention"},

    // Ghost Face
    {"ThrillingTremors", "Thrilling Tremors"},
    {"ImAllEars", "I'm All Ears"},
    {"FurtiveChase", "Furtive Chase"},

    // Demogorgon
    {"Surge", "Surge"},
    {"MindBreaker", "Mindbreaker"},
    {"CruelConfinement", "Cruel Limits"},

    // Oni
    {"ZanshinTactics", "Zanshin Tactics"},
    {"Nemesis", "Nemesis"},
    {"BloodEcho", "Blood Echo"},

    // Deathslinger
    {"HexRetribution", "Hex: Retribution"},
    {"Gearhead", "Gearhead"},
    {"DeadMansSwitch", "Dead Man's Switch"},

    // Executioner
    {"TrailOfTorment", "Trail of Torment"},
    {"ForcedPenance", "Forced Penance"},
    {"Deathbound", "Deathbound"},

    // Blight
    {"HexUndying", "Hex: Undying"},
    {"HexBloodFavor", "Hex: Blood Favour"},
    {"DragonsGrip", "Dragon's Grip"},

    // Twins
    {"K22P02", "Oppression"},
    {"K22P01", "Hoarder"},
    {"K22P03", "Coup De Grâce"},

    // Trickster
    {"K23P01", "Starstruck"},
    {"K23P03", "No Way Out"},
    {"K23P02", "Hex: Crowd Control"},

    // Nemesis
    {"K24P01", "Lethal Pursuer"},
    {"K24P02", "Hysteria"},
    {"K24P03", "Eruption"},

    // Pinhead
    {"K25P02", "Hex: Plaything"},
    {"K25P03", "Scourge Hook: Gift of Pain"},
    {"K25P01", "Deadlock"},

    // Artist
    {"K26P02", "Scourge Hook: Pain Resonance"},
    {"K26P03", "Hex: Pentimento"},
    {"K26P01", "Grim Embrace"},

    // Onryo
    {"K27P01", "Scourge Hook: Floods of Rage"},
    {"K27P03", "Merciless Storm"},
    {"K27P02", "Call of Brine"},

    // Dredge
    {"K28P03", "Septic Touch"},
    {"K28P01", "Dissolution"},
    {"K28P02", "Darkness Revealed"},

    // Mastermind
    {"K29P03", "Terminus"},
    {"K29P01", "Superior Anatomy"},
    {"K29P02", "Awakened Awareness"},

    // Knight
    {"K30P01", "Nowhere to Hide"},
    {"K30P03", "Hubris"},
    {"K30P02", "Hex: Face the Darkness"},

    // Skull Merchant
    {"K31P02", "Thwack!"},
    {"K31P03", "Leverage"},
    {"K31P01", "Game Afoot"},

    // Singularity
    {"K32P03", "Machine Learning"},
    {"K32P01", "Genetic Limits"},
    {"K32P02", "Forced Hesitation"},

    // Xenomorph
    {"K33P01", "Ultimate Weapon"},
    {"K33P02", "Rapid Brutality"},
    {"K33P03", "Alien Instinct"},

    // Good Guy
    {"K34P02", "Friends 'Til the End"},
    {"K34P03", "Batteries Included"},
    {"K34P01", "Hex: Two can Play"},

    // Unknown
    {"K35P02", "Unforeseen"},
    {"K35P03", "Undone"},
    {"K35P01", "Unbound"},

    // Lich
    {"K36P01", "Weave Attunement"},
    {"K36P02", "Languid Touch"},
    {"K36P03", "Dark Arrogance"},

    // Dark Lord
    {"K37P02", "Human Greed"},
    {"K37P01", "Hex: Wretched Fate"},
    {"K37P03", "Dominance"},

    // Houndmaster
    {"K38P02", "Scourge Hook: Jagged Compass"},
    {"K38P03", "No Quarter"},
    {"K38P01", "All-shaking Thunder"},

    // Ghoul
    {"K39P03", "None are Free"},
    {"K39P01", "Hex: Nothing but Misery"},
    {"K39P02", "Forever Entwined"},

    // Animatronic
    {"K40P02", "Phantom Fear"},
    {"K40P01", "Help Wanted"},
    {"K40P03", "Haywire"},

    // Krasue
    {"K41P02", "Wandering Eye"},
    {"K41P01", "Ravenous"},
    {"K41P03", "Hex: Overture of Doom"},

    // First
    {"K42P01", "Turn Back The Clock"},
    {"K42P02", "Secret Project"},
    {"K42P03", "Hex: Hive Mind"},
};

inline const char* DbdResolvePerkDisplayName(const std::string& raw_name) {
    auto it = g_Perks.find(raw_name);
    if (it != g_Perks.end())
        return it->second.data();
    return nullptr;
}

#endif

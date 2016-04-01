#!/usr/bin/env python3

import subprocess
import re
import sys
import os.path
import urllib.parse
import urllib.request

country_names = {
    4: "Afghanistan",
    8: "Albania",
    10: "Antarctica",
    12: "Algeria",
    16: "American Samoa",
    20: "Andorra",
    24: "Angola",
    28: "Antigua and Barbuda",
    31: "Azerbaijan",
    32: "Argentina",
    36: "Australia",
    40: "Austria",
    44: "Bahamas",
    48: "Bahrain",
    50: "Bangladesh",
    51: "Armenia",
    52: "Barbados",
    56: "Belgium",
    60: "Bermuda",
    64: "Bhutan",
    68: "Bolivia (state)",
    70: "Bosnia and Herzegovina",
    72: "Botswana",
    74: "Bouvet Island",
    76: "Brazil",
    84: "Belize",
    86: "British Indian Ocean Territory",
    90: "Solomon Islands",
    92: "the British Virgin Islands",
    96: "Brunei",
    100: "Bulgaria",
    104: "Myanmar",
    108: "Burundi",
    112: "Belarus",
    116: "Cambodia",
    120: "Cameroon",
    124: "Canada",
    132: "Cape Verde",
    136: "Cayman Islands",
    140: "Central African Republic",
    144: "Sri Lanka",
    148: "Chad",
    152: "Chile",
    156: "China",
    158: "the Republic of China",
    162: "Christmas Island",
    166: "Cocos (Keeling) Islands",
    170: "Colombia",
    174: "Comoros",
    175: "Mayotte",
    178: "the Republic of the Congo",
    180: "the Democratic Republic of the Congo",
    184: "Cook Islands",
    188: "Costa Rica",
    191: "Croatia",
    192: "Cuba",
    196: "Cyprus",
    203: "Czech Republic",
    204: "Benin",
    208: "Denmark",
    212: "Dominica",
    214: "Dominican Republic",
    218: "Ecuador",
    222: "El Salvador",
    226: "Equatorial Guinea",
    231: "Ethiopia",
    232: "Eritrea",
    233: "Estonia",
    234: "Faroe Islands",
    238: "the Falkland Islands",
    239: "South Georgia and the South Sandwich Islands",
    242: "Fiji",
    246: "Finland",
    248: "Åland Islands",
    250: "France",
    254: "French Guiana",
    258: "French Polynesia",
    260: "French Southern Territories",
    262: "Djibouti",
    266: "Gabon",
    268: "Georgia",
    270: "Gambia",
    275: "Palestine",
    276: "Germany",
    288: "Ghana",
    292: "Gibraltar",
    296: "Kiribati",
    300: "Greece",
    304: "Greenland",
    308: "Grenada",
    312: "Guadeloupe",
    316: "Guam",
    320: "Guatemala",
    324: "Guinea",
    328: "Guyana",
    332: "Haiti",
    334: "Heard Island and McDonald Islands",
    336: "the Vatican City",
    340: "Honduras",
    344: "Hong Kong",
    348: "Hungary",
    352: "Iceland",
    356: "India",
    360: "Indonesia",
    364: "Iran",
    368: "Iraq",
    372: "Ireland",
    376: "Israel",
    380: "Italy",
    384: "Côte d'Ivoire",
    388: "Jamaica",
    392: "Japan",
    398: "Kazakhstan",
    400: "Jordan",
    404: "Kenya",
    408: "North Korea",
    410: "South Korea",
    414: "Kuwait",
    417: "Kyrgyzstan",
    418: "Laos",
    422: "Lebanon",
    426: "Lesotho",
    428: "Latvia",
    430: "Liberia",
    434: "Libya",
    438: "Liechtenstein",
    440: "Lithuania",
    442: "Luxembourg",
    446: "Macao",
    450: "Madagascar",
    454: "Malawi",
    458: "Malaysia",
    462: "Maldives",
    466: "Mali",
    470: "Malta",
    474: "Martinique",
    478: "Mauritania",
    480: "Mauritius",
    484: "Mexico",
    492: "Monaco",
    496: "Mongolia",
    498: "Moldova",
    499: "Montenegro",
    500: "Montserrat",
    504: "Morocco",
    508: "Mozambique",
    512: "Oman",
    516: "Namibia",
    520: "Nauru",
    524: "Nepal",
    528: "Netherlands",
    531: "Curaçao",
    533: "Aruba",
    534: "Sint Maarten",
    535: "Bonaire, Sint Eustatius and Saba",
    540: "New Caledonia",
    548: "Vanuatu",
    554: "New Zealand",
    558: "Nicaragua",
    562: "Niger",
    566: "Nigeria",
    570: "Niue",
    574: "Norfolk Island",
    578: "Norway",
    580: "Northern Mariana Islands",
    581: "United States Minor Outlying Islands",
    583: "the Federated States of Micronesia",
    584: "Marshall Islands",
    585: "Palau",
    586: "Pakistan",
    591: "Panama",
    598: "Papua New Guinea",
    600: "Paraguay",
    604: "Peru",
    608: "Philippines",
    612: "the Pitcairn Islands",
    616: "Poland",
    620: "Portugal",
    624: "Guinea-Bissau",
    626: "Timor-Leste",
    630: "Puerto Rico",
    634: "Qatar",
    638: "Réunion",
    642: "Romania",
    643: "Russian Federation",
    646: "Rwanda",
    652: "Saint Barthélemy",
    654: "Saint Helena, Ascension and Tristan da Cunha",
    659: "Saint Kitts and Nevis",
    660: "Anguilla",
    662: "Saint Lucia",
    663: "Saint Martin (French part)",
    666: "Saint Pierre and Miquelon",
    670: "Saint Vincent and the Grenadines",
    674: "San Marino",
    678: "Sao Tome and Principe",
    682: "Saudi Arabia",
    686: "Senegal",
    688: "Serbia",
    690: "Seychelles",
    694: "Sierra Leone",
    702: "Singapore",
    703: "Slovakia",
    704: "Vietnam",
    705: "Slovenia",
    706: "Somalia",
    710: "South Africa",
    716: "Zimbabwe",
    724: "Spain",
    728: "South Sudan",
    729: "Sudan",
    732: "Western Sahara",
    740: "Suriname",
    744: "Svalbard and Jan Mayen",
    748: "Swaziland",
    752: "Sweden",
    756: "Switzerland",
    760: "Syria",
    762: "Tajikistan",
    764: "Thailand",
    768: "Togo",
    772: "Tokelau",
    776: "Tonga",
    780: "Trinidad and Tobago",
    784: "United Arab Emirates",
    788: "Tunisia",
    792: "Turkey",
    795: "Turkmenistan",
    796: "Turks and Caicos Islands",
    798: "Tuvalu",
    800: "Uganda",
    804: "Ukraine",
    807: "Macedonia",
    818: "Egypt",
    826: "United Kingdom",
    831: "Guernsey",
    832: "Jersey",
    833: "Isle of Man",
    834: "Tanzania",
    840: "United States of America",
    850: "United States Virgin Islands",
    854: "Burkina Faso",
    858: "Uruguay",
    860: "Uzbekistan",
    862: "Venezuela",
    876: "Wallis and Futuna",
    882: "Samoa",
    887: "Yemen",
    894: "Zambia"
}

original_file_re = re.compile(
    r'<a +href="(https://upload\.wikimedia\.org/wikipedia/commons/[^"]+)"'
    r'[^>]*>Original file</a>')

def get_flag_from_name(flag_name, filename):
    flag_name = flag_name.replace(' ', '_')
    quoted_flag_name = urllib.parse.quote(flag_name)
    details_url = ("https://commons.wikimedia.org/wiki/File:Flag_of_{}.svg".
                   format(quoted_flag_name))
    try:
        response = urllib.request.urlopen(details_url)
    except urllib.error.HTTPError as e:
        if e.code == 404:
            raise Exception("No article found for " + flag_name + " ( " +
                            details_url + " )")
        else:
            raise

    for line in response:
        line = str(line, 'utf-8')
        md = original_file_re.search(line)
        if md:
            res = subprocess.call(["curl", "-L", "-o", filename, md.group(1)])
            if res != 0:
                os.unlink(filename)
                raise Exception("curl failed")
            return

    raise Exception("Original file link not found in " + flag_name)

def get_country_flag(iso_code, filename):
    if iso_code not in country_names:
        raise Exception("Missing ISO code " + str(iso_code))

    get_flag_from_name(country_names[iso_code], filename)

def get_three_letter_code(numeric_code):
    result = ""
    for i in range(0, 3):
        letter = numeric_code // 27 ** (2 - i) % 27
        if letter == 0:
            break
        result += chr(letter + ord('a') - 1)
    return result

def get_subdivision_flag(iso_code, subdivision_code, filename):
    subdivision_code = get_three_letter_code(subdivision_code)
    country = country_names[iso_code]

    flag_map = {
        'Belgium' : {
            'vlg' : 'Flanders',
            'wal' : 'Wallonia'
        },
        'Canada' : {
            'qc' : 'Quebec'
        },
        'France' : {
            'n' : 'Midi-Pyrénées',
            'e' : 'Brittany'
        },
        'Spain' : {
            'ct' : 'Catalonia',
            'ga' : 'Galicia',
            'pv' : 'the Basque Country'
        },
        'United Kingdom' : {
            'sct' : 'Scotland',
            'wls' : 'Wales'
        }
    }

    flag_name = flag_map[country][subdivision_code]

    get_flag_from_name(flag_name, filename)

def get_language_flag(language_code, filename):
    language_code = get_three_letter_code(language_code)

    flag_map = {
        'epo' : 'Esperanto',
        'ido' : 'Ido',
        'ina' : 'Interlingua'
    }

    flag_name = flag_map[language_code]

    get_flag_from_name(flag_name, filename)

header_proc = subprocess.Popen(args = ["gcc", "-E", "fv-flag.h", "-o", "-"],
                               stdout = subprocess.PIPE,
                               cwd = "../../common")
source_proc = subprocess.Popen(args = ["gcc", "-o",
                                       "get-numbers",
                                       "-x", "c",
                                       "-"],
                               stdin = subprocess.PIPE)
source_proc.stdin.write("#include <stdio.h>\n"
                        "int main(int argc, char **argv) {\n".encode('utf-8'))

flag_re = re.compile(r'\b(FV_FLAG_[A-Z0-9_]+)\s*=\s*([^,]+),')

for line in header_proc.stdout:
    for md in flag_re.finditer(str(line, 'utf-8')):
        print_line = ("#define " + md.group(1) + " (" + md.group(2) + ")\n"
                      "printf(\"%i\\n\", " + md.group(1) + ");\n")
        source_proc.stdin.write(print_line.encode('utf-8'))

if header_proc.wait() != 0:
    sys.stderr.write("cpp failed\n")
    sys.exit(1)

source_proc.stdin.write("}\n".encode('utf-8'))
source_proc.stdin.close()
if source_proc.wait() != 0:
    sys.stderr.write("gcc failed\n")
    sys.exit(1)

num_proc = subprocess.Popen(args = ["./get-numbers"],
                            stdout = subprocess.PIPE)

for line in num_proc.stdout:
    num = int(str(line.rstrip(), 'utf-8'))

    filename = "flags/{:08x}.svg".format(num)
    if os.path.isfile(filename):
        continue

    if (num & 0x40000000) == 0:
        country_code = (num & 0x3fff0000) >> 16

        if (num & 0xffff) == 0:
            get_country_flag(country_code, filename)
        else:
            get_subdivision_flag(country_code, num & 0xffff, filename)
    else:
        get_language_flag(num & ~0x40000000, filename)

if num_proc.wait() != 0:
    sys.stderr.write("get-numbers failed\n")
    sys.exit(1)

os.unlink('./get-numbers')

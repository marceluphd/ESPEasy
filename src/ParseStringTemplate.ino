/********************************************************************************************\
  Parse string template
  \*********************************************************************************************/
String parseTemplate(String &tmpString, byte lineSize)
{
  checkRAM(F("parseTemplate"));
  String newString = "";
  String tmpStringMid = "";
  newString.reserve(lineSize);

  // replace task template variables
  int leftBracketIndex = tmpString.indexOf('[');
  if (leftBracketIndex == -1)
    newString = tmpString;
  else
  {
    byte count = 0;
    byte currentTaskIndex = ExtraTaskSettings.TaskIndex;

    while (leftBracketIndex >= 0 && count < 10 - 1)
    {
      newString += tmpString.substring(0, leftBracketIndex);
      tmpString = tmpString.substring(leftBracketIndex + 1);
      int rightBracketIndex = tmpString.indexOf(']');
      if (rightBracketIndex >= 0)
      {
        tmpStringMid = tmpString.substring(0, rightBracketIndex);
        tmpString = tmpString.substring(rightBracketIndex + 1);
        int hashtagIndex = tmpStringMid.indexOf('#');
        if (hashtagIndex >= 0) {
          String deviceName = tmpStringMid.substring(0, hashtagIndex);
          String valueName = tmpStringMid.substring(hashtagIndex + 1);
          String valueFormat = "";
          hashtagIndex = valueName.indexOf('#');
          if (hashtagIndex >= 0)
          {
            valueFormat = valueName.substring(hashtagIndex + 1);
            valueName = valueName.substring(0, hashtagIndex);
          }

          if (deviceName.equalsIgnoreCase(F("Plugin")))
          {
            String tmpString = tmpStringMid.substring(7);
            tmpString.replace('#', ',');
            if (PluginCall(PLUGIN_REQUEST, 0, tmpString))
              newString += tmpString;
          }
          else
            for (byte y = 0; y < TASKS_MAX; y++)
            {
              if (Settings.TaskDeviceEnabled[y])
              {
                LoadTaskSettings(y);
                String taskDeviceName = getTaskDeviceName(y);
                if (taskDeviceName.length() != 0)
                {
                  if (deviceName.equalsIgnoreCase(taskDeviceName))
                  {
                    boolean match = false;
                    for (byte z = 0; z < VARS_PER_TASK; z++)
                      if (valueName.equalsIgnoreCase(ExtraTaskSettings.TaskDeviceValueNames[z]))
                      {
                        match = true;
												// Try to format and transform the values
												// y = taskNr
												// z = var_of_task
												bool isvalid;
												String value = formatUserVar(y, z, isvalid);
												if (isvalid) {
												  transformValue(newString, lineSize, value, valueFormat, tmpString);
                          break;
                        }
                      }
                    if (!match) // try if this is a get config request
                    {
                      struct EventStruct TempEvent;
                      TempEvent.TaskIndex = y;
                      String tmpName = valueName;
                      if (PluginCall(PLUGIN_GET_CONFIG, &TempEvent, tmpName))
                        newString += tmpName;
                    }
                    break;
                  }
                }
              }
            }
        }
      }
      leftBracketIndex = tmpString.indexOf('[');
      count++;
    }
    checkRAM(F("parseTemplate2"));
    newString += tmpString;

    if (currentTaskIndex != 255)
      LoadTaskSettings(currentTaskIndex);
  }

  parseSystemVariables(newString, false);
  parseStandardConversions(newString, false);

  // padding spaces
  while (newString.length() < lineSize)
    newString += " ";
  checkRAM(F("parseTemplate3"));
  return newString;
}



/********************************************************************************************\
  Transform values
  \*********************************************************************************************/

// Syntax: [task#value#transformation#justification]
// valueFormat="transformation#justification"
void transformValue(
	String& newString,
  byte lineSize,
	String value,
	String& valueFormat,
  const String &tmpString)
{
  checkRAM(F("transformValue"));
  // here we know the task and value, so find the uservar

	// start changes by giig1967g - 2018-04-20
	// Syntax: [task#value#transformation#justification]
	// valueFormat="transformation#justification"
	if (valueFormat.length() > 0) { //do the checks only if a Format is defined to optimize loop
		String valueJust = "";

		int hashtagIndex = valueFormat.indexOf('#');
		if (hashtagIndex >= 0) {
			valueJust = valueFormat.substring(hashtagIndex + 1); //Justification part
			valueFormat = valueFormat.substring(0, hashtagIndex); //Transformation part
		}

		// valueFormat="transformation"
		// valueJust="justification"
		if (valueFormat.length() > 0) { //do the checks only if a Format is defined to optimize loop
			const int val = value == "0" ? 0 : 1; //to be used for GPIO status (0 or 1)
			const float valFloat = value.toFloat();

			String tempValueFormat = valueFormat;
			int tempValueFormatLength = tempValueFormat.length();
			const int invertedIndex = tempValueFormat.indexOf('!');
			const bool inverted = invertedIndex >= 0 ? 1 : 0;
			if (inverted)
				tempValueFormat.remove(invertedIndex, 1);

			const int rightJustifyIndex = tempValueFormat.indexOf('R');
			const bool rightJustify = rightJustifyIndex >= 0 ? 1 : 0;
			if (rightJustify)
				tempValueFormat.remove(rightJustifyIndex, 1);

			tempValueFormatLength = tempValueFormat.length(); //needed because could have been changed after '!' and 'R' removal

			//Check Transformation syntax
			if (tempValueFormatLength > 0) {
				switch (tempValueFormat[0]) {
				case 'V': //value = value without transformations
					break;
				case 'O':
					value = val == inverted ? F("OFF") : F(" ON"); //(equivalent to XOR operator)
					break;
				case 'C':
					value = val == inverted ? F("CLOSE") : F(" OPEN");
					break;
				case 'M':
					value = val == inverted ? F("AUTO") : F(" MAN");
					break;
				case 'm':
					value = val == inverted ? F("A") : F("M");
					break;
				case 'H':
					value = val == inverted ? F("COLD") : F(" HOT");
					break;
				case 'U':
					value = val == inverted ? F("DOWN") : F("  UP");
					break;
				case 'u':
					value = val == inverted ? F("D") : F("U");
					break;
				case 'Y':
					value = val == inverted ? F(" NO") : F("YES");
					break;
				case 'y':
					value = val == inverted ? F("N") : F("Y");
					break;
				case 'X':
					value = val == inverted ? F("O") : F("X");
					break;
				case 'I':
					value = val == inverted ? F("OUT") : F(" IN");
					break;
				case 'Z':// return "0" or "1"
					value = val == inverted ? F("0") : F("1");
					break;
				case 'D'://Dx.y min 'x' digits zero filled & 'y' decimal fixed digits
				{
					int x;
					int y;
					x = 0;
					y = 0;

					switch (tempValueFormatLength) {
					case 2: //Dx
						if (isDigit(tempValueFormat[1])) {
							x = (int)tempValueFormat[1] - '0';
						}
						break;
					case 3: //D.y
						if (tempValueFormat[1] == '.' && isDigit(tempValueFormat[2])) {
							y = (int)tempValueFormat[2] - '0';
						}
						break;
					case 4: //Dx.y
						if (isDigit(tempValueFormat[1]) && tempValueFormat[2] == '.' && isDigit(tempValueFormat[3])) {
							x = (int)tempValueFormat[1] - '0';
							y = (int)tempValueFormat[3] - '0';
						}
						break;
					case 1: //D
					default: //any other combination x=0; y=0;
						break;
					}
					value = toString(valFloat, y);
					int indexDot;
					indexDot = value.indexOf('.') > 0 ? value.indexOf('.') : value.length();
					for (byte f = 0; f < (x - indexDot); f++)
						value = "0" + value;
					break;
				}
				case 'F':// FLOOR (round down)
					value = (int)floorf(valFloat);
					break;
				case 'E':// CEILING (round up)
					value = (int)ceilf(valFloat);
					break;
				default:
					value = F("ERR");
					break;
				}

				// Check Justification syntax
				const int valueJustLength = valueJust.length();
				if (valueJustLength > 0) { //do the checks only if a Justification is defined to optimize loop
					value.trim(); //remove right justification spaces for backward compatibility
					switch (valueJust[0]) {
					case 'P':// Prefix Fill with n spaces: Pn
						if (valueJustLength > 1) {
							if (isDigit(valueJust[1])) { //Check Pn where n is between 0 and 9
								int filler = valueJust[1] - value.length() - '0'; //char '0' = 48; char '9' = 58
								for (byte f = 0; f < filler; f++)
									newString += " ";
							}
						}
						break;
					case 'S':// Suffix Fill with n spaces: Sn
						if (valueJustLength > 1) {
							if (isDigit(valueJust[1])) { //Check Sn where n is between 0 and 9
								int filler = valueJust[1] - value.length() - '0'; //48
								for (byte f = 0; f < filler; f++)
									value += " ";
							}
						}
						break;
					case 'L': //left part of the string
						if (valueJustLength > 1) {
							if (isDigit(valueJust[1])) { //Check n where n is between 0 and 9
								value = value.substring(0, (int)valueJust[1] - '0');
							}
						}
						break;
					case 'R': //Right part of the string
						if (valueJustLength > 1) {
							if (isDigit(valueJust[1])) { //Check n where n is between 0 and 9
								value = value.substring(std::max(0, (int)value.length() - ((int)valueJust[1] - '0')));
							}
						}
						break;
					case 'U': //Substring Ux.y where x=firstChar and y=number of characters
						if (valueJustLength > 1) {
							if (isDigit(valueJust[1]) && valueJust[2] == '.' && isDigit(valueJust[3]) && valueJust[1] > '0' && valueJust[3] > '0') {
								value = value.substring(std::min((int)value.length(), (int)valueJust[1] - '0' - 1), (int)valueJust[1] - '0' - 1 + (int)valueJust[3] - '0');
							}else  {
								newString += F("ERR");
							}
						}
						break;
					default:
						newString += F("ERR");
						break;
					}
				}
			}
			if (rightJustify) {
				int filler = lineSize - newString.length() - value.length() - tmpString.length();
				for (byte f = 0; f < filler; f++)
					newString += " ";
			}
			{
				if (loglevelActiveFor(LOG_LEVEL_DEBUG)) {
					String logFormatted = F("DEBUG: Formatted String='");
					logFormatted += newString;
					logFormatted += value;
					logFormatted += "'";
					addLog(LOG_LEVEL_DEBUG, logFormatted);
				}
			}
		}
	}
	//end of changes by giig1967g - 2018-04-18

	newString += String(value);
	{
		if (loglevelActiveFor(LOG_LEVEL_DEBUG_DEV)) {
			String logParsed = F("DEBUG DEV: Parsed String='");
			logParsed += newString;
			logParsed += "'";
			addLog(LOG_LEVEL_DEBUG_DEV, logParsed);
		}
	}
  checkRAM(F("transformValue2"));
}

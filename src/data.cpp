#include "data.h"

static size_t WriteCallback(char *ptr, size_t size, size_t nmemb, std::string *userdata)
{
    userdata->append(ptr, size * nmemb);
    return size * nmemb;
}

std::time_t getDurationInSeconds(const std::string &duration) 
{
    // This supports duration formats like 1d, 2 weeks, 1y, 3mo, 1 month, etc.
    std::regex pattern(R"(\s*(\d+)\s*([yYmMdDwW]+(?:\w*)?)\s*)");
    std::unordered_map<std::string, std::time_t> unitToSeconds = {
        {"y", 31536000},  // year in seconds
        {"m", 2592000},   // month in seconds
        {"w", 604800},    // week in seconds
        {"d", 86400}      // day in seconds
    };

    std::smatch match;
    if (std::regex_match(duration, match, pattern)) {
        int value = std::stoi(match[1]);
        std::string unit = match[2].str();
        unit = tolower(unit[0]); // Only interested in the first letter of the unit (y/m/w/d)
        if (unitToSeconds.find(unit) != unitToSeconds.end()) {
            return value * unitToSeconds[unit];
        }
    }

    std::cerr << "Invalid duration format. Examples of supported formats: 7m, 1w, 3y, 6d, where m = month, w = week, y = year, and d = day." << std::endl;
    return 0;
}

std::time_t convertToUnixTimestamp(const std::string &date)
{
    std::tm timeStruct = {};
    std::istringstream ss(date);
    ss >> std::get_time(&timeStruct, "%d/%m/%Y");

    if (ss.fail())
    {
        std::cerr << "Failed to parse the date: " << date << std::endl;
        return 0;
    }

    std::time_t timestamp = std::mktime(&timeStruct);
    return timestamp;
}

std::string httpGet(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        // Set the User-Agent header
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36");
        // Set the callback function to handle the response data
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        // Pass the string to write the response data to
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

std::vector<std::vector<std::string>> fetchOHLCData(const std::string &symbol, const std::string &duration)
{
    // Define the current timestamp as the end time
    std::time_t endTime = std::time(nullptr);

    // Calculate the start time based on the duration
    std::time_t startTime = endTime;
    startTime -= getDurationInSeconds(duration);
    if (startTime == endTime)
    {
        std::cerr << "Invalid duration format. Examples of supported formats: 7m, 1w, 3y, 6d, where m = month, w = week, y = year, and d = day." << std::endl;
        return std::vector<std::vector<std::string>>();
    }

    // Convert timestamps to strings
    std::ostringstream startTimestamp, endTimestamp;
    startTimestamp << startTime;
    endTimestamp << endTime;

    // Build the URL for historical data
    std::string url = "https://query1.finance.yahoo.com/v7/finance/download/" + symbol +
                      "?period1=" + startTimestamp.str() + "&period2=" + endTimestamp.str() +
                      "&interval=1d&events=history";

    // Fetch historical price data using the URL
    std::string response = httpGet(url);

    // Check if response contains an error or is empty
    if (response.find("404 Not Found: No data found, symbol may be delisted") != std::string::npos)
    {
        std::cerr << "Symbol not found or delisted: " << symbol << std::endl;
        return std::vector<std::vector<std::string>>();
    }
    if (response.empty())
    {
        std::cerr << "Failed to fetch data from the server." << std::endl;
        return std::vector<std::vector<std::string>>();
    }

    // Parse the CSV response and store OHLC data in a 2D vector
    std::istringstream ss(response);
    std::string line;

    // Skip the header line
    std::getline(ss, line);

    std::vector<std::vector<std::string>> ohlcData; // 2D vector to store OHLC data

    while (std::getline(ss, line))
    {
        std::istringstream lineStream(line);
        std::string date, open, high, low, close, adjClose, volume;
        std::getline(lineStream, date, ',');
        std::getline(lineStream, open, ',');
        std::getline(lineStream, high, ',');
        std::getline(lineStream, low, ',');
        std::getline(lineStream, close, ',');
        std::getline(lineStream, adjClose, ',');
        std::getline(lineStream, volume, ',');

        // Store OHLC data for this row in a vector (as strings)
        std::vector<std::string> ohlcRow = {date, open, high, low, close, volume};
        ohlcData.push_back(ohlcRow);
    }

    return ohlcData;
}

std::string getFormattedPrice(const std::string &symbol, bool markdown, bool closedWarning)
{
    // Fetch data
    Metrics data = fetchMetrics(symbol);

    // Check if there is a price
    if (data.latestPrice == 0)
    {
        return "Could not fetch latest price data. Symbol may be invalid.";
    }

    // Create the formatted string with the result
    std::ostringstream resultStream;
    if (!markdown)
    {
        resultStream << "Latest Price for " << data.name << ": " << std::fixed << std::setprecision(2) << data.latestPrice << " " << data.currency
                     << " (" << (data.latestChange >= 0 ? "+" : "") << std::fixed << std::setprecision(2) << data.latestChange << "%)";
        if (closedWarning == true && data.marketState != "REGULAR") // Add note if market is not open
        {
            resultStream << " *Market is currently closed*" << std::endl;
        }
        else
        {
            resultStream << "\n"; // closedWarning adds new line, so in case of no warning, add close line here
        }
    }
    else
    {
        resultStream << "### Latest Price for " << data.name << "\n`" << std::fixed << std::setprecision(2) << data.latestPrice << " " << data.currency;
        
        if (data.latestChange >= 0)
        {
            resultStream << " (+" << std::fixed << std::setprecision(2) << data.latestChange << "%)`:chart_with_upwards_trend:";
        }
        else
        {
            resultStream << " (" << std::fixed << std::setprecision(2) << data.latestChange << "%)`:chart_with_downwards_trend:";
        }

        if (closedWarning == true && data.marketState != "REGULAR") // Add note if market is not open
        {
            resultStream << " *Market is currently closed*" << std::endl;
        }
    }

    return resultStream.str();
}

Metrics fetchMetrics(const std::string &symbol)
{
    Metrics equityMetrics;

    // Construct the Yahoo Finance API URL with the symbol
    std::string apiUrl = "https://query1.finance.yahoo.com/v6/finance/options/" + symbol;

    // Fetch data from the Yahoo Finance API
    std::string response = httpGet(apiUrl);

    // Check if response contains an error or is empty
    if (response.find("{\"optionChain\":{\"result\":[],\"error\":null}}") != std::string::npos)
    {
        std::cerr << "Symbol not found or delisted: " << symbol << std::endl;
        return equityMetrics;
    }
    if (response.empty())
    {
        std::cerr << "Failed to fetch data from the server." << std::endl;
        return equityMetrics;
    }

    // Parse the JSON response
    rapidjson::Document document;
    document.Parse(response.c_str());

    if (!document.HasParseError() && document.HasMember("optionChain"))
    {
        const rapidjson::Value &optionChain = document["optionChain"]["result"][0];
        const rapidjson::Value &quote = optionChain["quote"];

        // Extract metrics from quote object
        if ((quote.HasMember("shortName") && quote["shortName"].IsString())) // name from shortName
        {
            equityMetrics.name = quote["shortName"].GetString();
        }
        if (quote.HasMember("symbol") && quote["symbol"].IsString()) // symbol
        {
            equityMetrics.symbol = quote["symbol"].GetString();
        }
        if (quote.HasMember("currency") && quote["currency"].IsString()) // currency
        {
            equityMetrics.currency = quote["currency"].GetString();
        }
        if (quote.HasMember("marketState") && quote["marketState"].IsString()) // marketState
        {
            equityMetrics.marketState = quote["marketState"].GetString();
        }
        if (quote.HasMember("marketCap") && quote["marketCap"].IsNumber()) // marketCap
        {
            equityMetrics.marketCap = quote["marketCap"].GetDouble();
        }
        if (quote.HasMember("trailingPE") && quote["trailingPE"].IsNumber()) // peRatio
        {
            equityMetrics.peRatio = quote["trailingPE"].GetDouble();
        }
        if (quote.HasMember("regularMarketPrice") && quote["regularMarketPrice"].IsNumber()) // latestPrice
        {
            equityMetrics.latestPrice = quote["regularMarketPrice"].GetDouble();
        }
        if (quote.HasMember("regularMarketChangePercent") && quote["regularMarketChangePercent"].IsNumber()) // latestChange
        {
            equityMetrics.latestChange = quote["regularMarketChangePercent"].GetDouble();
        }
        if (quote.HasMember("regularMarketOpen") && quote["regularMarketOpen"].IsNumber()) // openPrice
        {
            equityMetrics.openPrice = quote["regularMarketOpen"].GetDouble();
        }
        if (quote.HasMember("regularMarketDayLow") && quote["regularMarketDayLow"].IsNumber()) // dayLow
        {
            equityMetrics.dayLow = quote["regularMarketDayLow"].GetDouble();
        }
        if (quote.HasMember("regularMarketDayHigh") && quote["regularMarketDayHigh"].IsNumber()) // dayHigh
        {
            equityMetrics.dayHigh = quote["regularMarketDayHigh"].GetDouble();
        }
        if (quote.HasMember("regularMarketPreviousClose") && quote["regularMarketPreviousClose"].IsNumber()) // prevClose
        {
            equityMetrics.prevClose = quote["regularMarketPreviousClose"].GetDouble();
        }
        if (quote.HasMember("fiftyTwoWeekLow") && quote["fiftyTwoWeekLow"].IsNumber()) // fiftyTwoWeekLow
        {
            equityMetrics.fiftyTwoWeekLow = quote["fiftyTwoWeekLow"].GetDouble();
        }
        if (quote.HasMember("fiftyTwoWeekHigh") && quote["fiftyTwoWeekHigh"].IsNumber()) // fiftyTwoWeekHigh
        {
            equityMetrics.fiftyTwoWeekHigh = quote["fiftyTwoWeekHigh"].GetDouble();
        }
        if (quote.HasMember("fiftyDayAverage") && quote["fiftyDayAverage"].IsNumber()) // avg_50
        {
            equityMetrics.avg_50 = quote["fiftyDayAverage"].GetDouble();
        }
        if (quote.HasMember("twoHundredDayAverage") && quote["twoHundredDayAverage"].IsNumber()) // avg_200
        {
            equityMetrics.avg_200 = quote["twoHundredDayAverage"].GetDouble();
        }
        if (quote.HasMember("averageDailyVolume3Month") && quote["averageDailyVolume3Month"].IsNumber()) // avgVol_3mo
        {
            equityMetrics.avgVol_3mo = quote["averageDailyVolume3Month"].GetDouble();
        }
    }
    else
    {
        std::cerr << "JSON parsing error or missing member" << std::endl;
    }

    return equityMetrics;
}

std::string getFormattedMetrics(const std::string &symbol, bool markdown)
{
    // Fetch metrics for the given symbol
    Metrics metrics = fetchMetrics(symbol);

    // Check if data is valid
    if (metrics.symbol == "-")
    {
        return "Could not fetch data. Symbol may be invalid.";
    }

    // Create a string stream to hold the formatted metrics
    std::ostringstream formattedMetrics;

    // Format the metrics
    if (!markdown)
    {
        formattedMetrics << "Metrics for " << metrics.name << ":\n";
        formattedMetrics << std::fixed << std::setprecision(2);
        formattedMetrics << "- Market Cap:         " << metrics.marketCap << " " << metrics.currency << "\n";
        formattedMetrics << "- P/E Ratio:          " << metrics.peRatio << "\n";
        formattedMetrics << "- Latest Price:       " << metrics.latestPrice << " " << metrics.currency << "\n";
        formattedMetrics << "- Open price:         " << metrics.openPrice << " " << metrics.currency << "\n";
        formattedMetrics << "- Day Low:            " << metrics.dayLow << " " << metrics.currency << "\n";
        formattedMetrics << "- Day High:           " << metrics.dayHigh << " " << metrics.currency << "\n";
        formattedMetrics << "- Previous Close:     " << metrics.prevClose << " " << metrics.currency << "\n";
        formattedMetrics << "- 52 Week Low:        " << metrics.fiftyTwoWeekLow << " " << metrics.currency << "\n";
        formattedMetrics << "- 52 Week High:       " << metrics.fiftyTwoWeekHigh << " " << metrics.currency << "\n";
        formattedMetrics << "- 50 Day avg:         " << metrics.avg_50 << " " << metrics.currency << "\n";
        formattedMetrics << "- 200 Day avg:        " << metrics.avg_200 << " " << metrics.currency << "\n";
        formattedMetrics << std::fixed << std::setprecision(0); // For average volume no need for decimal places
        formattedMetrics << "- 3 Month Volume avg: " << metrics.avgVol_3mo << "\n";
    }
    else // It looks weird but this way in Discord the spaces between the names and values are even
    {
        formattedMetrics << "### Metrics for " << metrics.name << "\n";
        formattedMetrics << std::fixed << std::setprecision(2);
        formattedMetrics << "- Market Cap:                          `" << metrics.marketCap << " " << metrics.currency << "`\n";
        formattedMetrics << "- P/E Ratio:                              `" << metrics.peRatio << "`\n";
        formattedMetrics << "- Latest Price:                         `" << metrics.latestPrice << " " << metrics.currency << "`\n";
        formattedMetrics << "- Open price:                           `" << metrics.openPrice << " " << metrics.currency << "`\n";
        formattedMetrics << "- Day Low:                                `" << metrics.dayLow << " " << metrics.currency << "`\n";
        formattedMetrics << "- Day High:                               `" << metrics.dayHigh << " " << metrics.currency << "`\n";
        formattedMetrics << "- Previous Close:                    `" << metrics.prevClose << " " << metrics.currency << "`\n";
        formattedMetrics << "- 52 Week Low:                       `" << metrics.fiftyTwoWeekLow << " " << metrics.currency << "`\n";
        formattedMetrics << "- 52 Week High:                      `" << metrics.fiftyTwoWeekHigh << " " << metrics.currency << "`\n";
        formattedMetrics << "- 50 Day avg:                           `" << metrics.avg_50 << " " << metrics.currency << "`\n";
        formattedMetrics << "- 200 Day avg:                         `" << metrics.avg_200 << " " << metrics.currency << "`\n";
        formattedMetrics << std::fixed << std::setprecision(0); // For average volume no need for decimal places
        formattedMetrics << "- 3 Month Volume avg:         `" << metrics.avgVol_3mo << "`\n";
    }

    // Return the formatted metrics as a string
    return formattedMetrics.str();
}

std::string getFormattedPrices(std::vector<std::string> symbols, std::vector<std::string> names, std::vector<std::string> descriptions, bool markdown, bool closedWarning)
{
    // Check if data is available
    if (symbols.empty())
    {
        return "No data available.";
    }

    // Check if there are names and descriptions available
    bool addNames = false;
    bool addDescription = false;
    if ((!names.empty()) && (names.size() == symbols.size()))
    {
        addNames = true;
    }
    if ((!descriptions.empty()) && (descriptions.size() == symbols.size()))
    {
        addDescription = true;
    }

    std::ostringstream formattedString;

    for (int i = 0; i < symbols.size(); i++)
    {
        // First fetch price data
        Metrics data = fetchMetrics(symbols[i]);

        // Now create string
        if (!markdown)
        {
            if (addNames) // Display name
            {
                formattedString << names[i] << std::endl;
            }
            else // Otherwise just add the symbol
            {
                formattedString << symbols[i] << std::endl;
            }
            if (addDescription) // Add description if available
            {
                formattedString << descriptions[i] << std::endl;
            }
            formattedString << std::fixed << std::setprecision(2);
            formattedString << "- Latest price: " << data.latestPrice << " " << data.currency;
            if (data.latestChange >= 0)
            {
                formattedString << " (+" << data.latestChange << "%)";
            }
            else
            {
                formattedString << " (" << data.latestChange << "%)";
            }
            if (closedWarning == true && data.marketState != "REGULAR") // Add note if market is not open
            {
                formattedString << " *Market is currently closed*" << std::endl;
            }
            else
            {
                formattedString << "\n"; // closedWarning adds new line, so in case of no warning, add close line here
            }
        }
        else
        {
            if (addNames) // Display name
            {
                formattedString << "### " << names[i] << std::endl;
            }
            else // Otherwise just add the symbol
            {
                formattedString << "### " << symbols[i] << std::endl;
            }
            if (addDescription) // Add description if available
            {
                formattedString << descriptions[i] << std::endl;
            }
            formattedString << std::fixed << std::setprecision(2);
            formattedString << "- Latest price: `" << data.latestPrice << " " << data.currency;
            if (data.latestChange >= 0)
            {
                formattedString << " (+" << data.latestChange << "%)`:chart_with_upwards_trend:";
            }
            else
            {
                formattedString << " (" << data.latestChange << "%)`:chart_with_downwards_trend:";
            }

            if (closedWarning == true && data.marketState != "REGULAR") // Add note if market is not open
            {
                formattedString << " *Market is currently closed*" << std::endl;
            }
            else
            {
                formattedString << "\n"; // closedWarning adds new line, so in case of no warning, add close line here
            }
        }
    }

    return formattedString.str();
}

std::string getFormattedJSON(const std::string &pathToJson, const std::string &key, bool markdown, bool description, bool closedWarning)
{
    // Load JSON data from a file
    std::ifstream file(pathToJson);
    if (!file.is_open())
    {
        return "Error: Unable to open JSON file.";
    }

    // Read JSON data from the file
    std::string jsonData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    rapidjson::Document document;
    document.Parse(jsonData.c_str());

    if (!document.IsObject())
    {
        return "Error: Invalid JSON data.";
    }

    rapidjson::Value::ConstMemberIterator dataIt = document.FindMember(key.c_str());
    if (dataIt == document.MemberEnd() || !dataIt->value.IsArray())
    {
        return "Error: Invalid key.";
    }

    const rapidjson::Value &data = dataIt->value;

    std::vector<std::string> symbols;
    std::vector<std::string> names;
    std::vector<std::string> descriptions;

    // Put data in vectors
    for (rapidjson::SizeType i = 0; i < data.Size(); ++i)
    {
        const rapidjson::Value &jsonData = data[i];
        if (jsonData.HasMember("symbol") && jsonData.HasMember("name"))
        {
            symbols.push_back(jsonData["symbol"].GetString());
            names.push_back(jsonData["name"].GetString());
            if (description && jsonData.HasMember("description"))
            {
                descriptions.push_back(jsonData["description"].GetString());
            }
        }
    }

    return getFormattedPrices(symbols, names, descriptions, markdown, closedWarning);
}

std::string getFormattedGainsLosses(const std::string &symbol, const std::string &duration, bool markdown)
{
    // Fetch data
    Metrics data = fetchMetrics(symbol);
    std::vector<std::vector<std::string>> ohlcData = fetchOHLCData(symbol, duration);
    if (ohlcData.empty())
    {
        std::cout << "No OHLC data available." << std::endl;
        return "";
    }

    // Vector to store percentage changes with corresponding dates
    std::vector<GainLoss> changes;

    // Iterate over data and calculate the percentage change for each day
    for (const auto &day : ohlcData)
    {
        if (day.size() < 5) continue; // Ensure there's enough data
        const std::string &date = day[0];
        double open = std::stod(day[1]);
        double close = std::stod(day[4]);

        // Calculate percentage change
        double percentageChange = ((close - open) / open) * 100.0;
        changes.emplace_back(date, percentageChange, open, close);
    }

    // Sort changes to find top 5 gains and losses
    std::sort(changes.begin(), changes.end(), [](const GainLoss &a, const GainLoss &b) {
        return a.percentageChange > b.percentageChange;
    });

    // Extract top 5 gains
    std::vector<GainLoss> topGains(changes.begin(), changes.begin() + std::min<size_t>(5, changes.size()));
    // Extract top 5 losses
    std::vector<GainLoss> topLosses(changes.end() - std::min<size_t>(5, changes.size()), changes.end());

    // Sort losses from most negative to least negative
    std::sort(topLosses.begin(), topLosses.end(), [](const GainLoss &a, const GainLoss &b) {
        return a.percentageChange < b.percentageChange;
    });

    // Format the results
    std::ostringstream result;

    if (markdown)
    {
        result << "### Top 5 Gains :chart_with_upwards_trend:\n";
    }
    else
    {
        result << "Top 5 Gains:\n";
    }
    for (size_t i = 0; i < topGains.size(); ++i)
    {
        if (markdown)
        {
            result << i + 1 << ". " << topGains[i].date << ": `" 
                << std::fixed << std::setprecision(2) << topGains[i].percentageChange 
                << "% (open: " << topGains[i].openPrice << " " << data.currency 
                << ", close: " << topGains[i].closePrice << " " << data.currency << ")`\n";
        }
        else
        {
            result << i + 1 << ". " << topGains[i].date << ": " 
                << std::fixed << std::setprecision(2) << topGains[i].percentageChange 
                << "% (open: " << topGains[i].openPrice << " " << data.currency 
                << ", close: " << topGains[i].closePrice << " " << data.currency << ")\n";
        }
    }

    if (markdown)
    {
        result << "\n### Top 5 Losses :chart_with_downwards_trend:\n";
    }
    else
    {
        result << "\nTop 5 Losses:\n";
    }
    for (size_t i = 0; i < topLosses.size(); ++i)
    {
        if (markdown)
        {
            result << i + 1 << ". " << topLosses[i].date << ": `" 
                << std::fixed << std::setprecision(2) << topLosses[i].percentageChange 
                << "% (open: " << topLosses[i].openPrice << " " << data.currency 
                << ", close: " << topLosses[i].closePrice << " " << data.currency << ")`\n";
        }
        else
        {
            result << i + 1 << ". " << topLosses[i].date << ": " 
                << std::fixed << std::setprecision(2) << topLosses[i].percentageChange 
                << "% (open: " << topLosses[i].openPrice << " " << data.currency 
                << ", close: " << topLosses[i].closePrice << " " << data.currency << ")\n";
        }
    }

    return result.str();
}
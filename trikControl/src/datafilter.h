/* Copyright 2020 CyberTech Labs Ltd., Andrei Khodko
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */


#pragma once

#include <QObject>
#include <functional>

class DataFilter : public QObject
{
	Q_OBJECT

public:
	DataFilter() = default;

	DataFilter(int minValue, int maxValue, const QString &filterName = "");

	int applyFilter(int data);

private:
	int getMedian3(int data);

	std::function<int(int data)> mFilterFunction;

	int mReadData1 {-1};
	int mReadData2 {-1};
};

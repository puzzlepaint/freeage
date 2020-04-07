// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "about_dialog.hpp"

#include <QBoxLayout>
#include <QDebug>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>


LicenseBrowser::LicenseBrowser(QWidget* parent)
    : QTextBrowser(parent) {}

QVariant LicenseBrowser::loadResource(int type, const QUrl& name) {
  if (type == QTextDocument::HtmlResource) {
    QString urlString = name.toString();
    if (urlString == "license://freeage") {
      return QString("<h2>FreeAge</h2>"
"<br/>"
"Copyright 2020 The FreeAge authors (Thomas Sch&ouml;ps)<br/>"
"<br/>"
"Redistribution and use in source and binary forms, with or without<br/>"
"modification, are permitted provided that the following conditions are met:<br/>"
"<br/>"
"1. Redistributions of source code must retain the above copyright notice,<br/>"
"   this list of conditions and the following disclaimer.<br/>"
"<br/>"
"2. Redistributions in binary form must reproduce the above copyright notice,<br/>"
"   this list of conditions and the following disclaimer in the documentation<br/>"
"   and/or other materials provided with the distribution.<br/>"
"<br/>"
"3. Neither the name of the copyright holder nor the names of its contributors<br/>"
"   may be used to endorse or promote products derived from this software<br/>"
"   without specific prior written permission.<br/>"
"<br/>"
"THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\"<br/>"
"AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE<br/>"
"IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE<br/>"
"ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE<br/>"
"LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR<br/>"
"CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF<br/>"
"SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS<br/>"
"INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN<br/>"
"CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)<br/>"
"ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE<br/>"
"POSSIBILITY OF SUCH DAMAGE.<br/>"
"<br/>"
"<h3>Licenses of used third-party libraries</h3>"
"<ul>"
"<li><a href=\"license://loguru\">loguru</a></li>"
"<li><a href=\"license://mango\">mango</a></li>"
"<li><a href=\"license://googletest\">googletest</a></li>"
"<li><a href=\"license://qt5\">Qt5</a></li>"
"<li><a href=\"license://rectanglebinpack\">RectangleBinPack</a></li>"
"<li><a href=\"license://yaml-cpp\">yaml-cpp</a></li>"
"</ul>");
    } else if (urlString == "license://rectanglebinpack") {
      return QStringLiteral("<h3>RectangleBinPack</h3>") +
QString::fromLocal8Bit(R"LICENSETEXT(
Source: https://github.com/juj/RectangleBinPack/

This work is released to the Public Domain.)LICENSETEXT").toHtmlEscaped().replace('\n', "<br/>").replace(' ', "&nbsp;");
    } else if (urlString == "license://mango") {
      return QStringLiteral("<h3>mango</h3>") +
QString::fromLocal8Bit(R"LICENSETEXT(
MANGO
Copyright (c) 2012-2019 Twilight Finland 3D Oy Ltd. All rights reserved.

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.)LICENSETEXT").toHtmlEscaped().replace('\n', "<br/>").replace(' ', "&nbsp;");
    } else if (urlString == "license://loguru") {
      return QStringLiteral("<h3>loguru</h3>") +
QString::fromLocal8Bit(R"LICENSETEXT(
Loguru logging library for C++, by Emil Ernerfeldt.
www.github.com/emilk/loguru
If you find Loguru useful, please let me know on twitter or in a mail!
Twitter: @ernerfeldt
Mail:    emil.ernerfeldt@gmail.com
Website: www.ilikebigbits.com

# License
    This software is in the public domain. Where that dedication is not
    recognized, you are granted a perpetual, irrevocable license to
    copy, modify and distribute it as you see fit.)LICENSETEXT").toHtmlEscaped().replace('\n', "<br/>").replace(' ', "&nbsp;");
    } else if (urlString == "license://qt5") {
      return tr("<h3>Qt5</h3>Qt license information is shown as a dialog. Qt is used under the GNU LGPL licensing option.");
    } else if (urlString == "license://yaml-cpp") {
      return QStringLiteral("<h3>yaml-cpp</h3>") +
QString::fromLocal8Bit(R"LICENSETEXT(
Copyright (c) 2008-2015 Jesse Beder.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.)LICENSETEXT").toHtmlEscaped().replace('\n', "<br/>").replace(' ', "&nbsp;");
    } else if (urlString == "license://googletest") {
      return QStringLiteral("<h3>googletest</h3>") +
QString::fromLocal8Bit(R"LICENSETEXT(
Copyright 2008, Google Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
    * Neither the name of Google Inc. nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.)LICENSETEXT").toHtmlEscaped().replace('\n', "<br/>").replace(' ', "&nbsp;");
    } else {
      qDebug() << "Error: LicenseBrowser does not handle URL:" << urlString;
      return QTextBrowser::loadResource(type, name);
    }
  } else {
    return QTextBrowser::loadResource(type, name);
  }
}


AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent) {
  setWindowIcon(QIcon(":/free_age/free_age.png"));
  setWindowTitle(tr("About FreeAge"));
  
  textBrowser = new LicenseBrowser(this);
  textBrowser->setSource(QUrl("license://freeage"));
  textBrowser->home();
  
  backButton = new QPushButton(tr("Back"));
  backButton->setEnabled(false);
  QPushButton* closeButton = new QPushButton(tr("Close"));
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(textBrowser, 1);
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addWidget(backButton);
  buttonLayout->addStretch(1);
  buttonLayout->addWidget(closeButton);
  layout->addLayout(buttonLayout);
  setLayout(layout);
  
  resize(std::max(800, width()), std::max(600, height()));
  
  // --- Connections ---
  connect(textBrowser, &LicenseBrowser::backwardAvailable, backButton, &QPushButton::setEnabled);
  connect(textBrowser, &QTextBrowser::sourceChanged, [&](const QUrl& url) {
    if (url.toString() == "license://qt5") {
      QMessageBox::aboutQt(this);
    }
  });
  
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
  connect(backButton, &QPushButton::clicked, textBrowser, &QTextBrowser::backward);
}

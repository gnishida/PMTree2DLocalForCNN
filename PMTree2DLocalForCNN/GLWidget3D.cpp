﻿#include <iostream>
#include "GLWidget3D.h"
#include "MainWindow.h"
#include <GL/GLU.h>
#include <QTimer>
#include <glm/gtc/matrix_transform.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <QDir>
#include <QMessageBox>
#include <QTextStream>

GLWidget3D::GLWidget3D(MainWindow* mainWin) : QGLWidget(QGLFormat(QGL::SampleBuffers), (QWidget*)mainWin) {
	this->mainWin = mainWin;
	ctrlPressed = false;
	shiftPressed = false;
	altPressed = false;

	// 光源位置をセット
	// ShadowMappingは平行光源を使っている。この位置から原点方向を平行光源の方向とする。
	light_dir = glm::normalize(glm::vec3(-4, -5, -8));
	//light_dir = glm::normalize(glm::vec3(-1, -3, -2));

	// シャドウマップ用のmodel/view/projection行列を作成
	glm::mat4 light_pMatrix = glm::ortho<float>(-100, 100, -100, 100, 0.1, 200);
	glm::mat4 light_mvMatrix = glm::lookAt(-light_dir * 50.0f, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
	light_mvpMatrix = light_pMatrix * light_mvMatrix;
}

void GLWidget3D::generateTrainingData() {
	srand(2);

	QString resultDir = "C:\\Anaconda\\caffe\\data\\pmtree2dlocal\\pmtree2dlocal\\";

	if (QDir(resultDir).exists()) {
		QDir(resultDir).removeRecursively();
	}
	QDir().mkpath(resultDir);

	QFile file("C:\\Anaconda\\caffe\\data\\pmtree2dlocal\\pmtree2dlocal\\parameters.txt");
	if (!file.open(QIODevice::WriteOnly)) {
		QMessageBox::warning(this, "Warning", "Output directory is not accessible.");

		return;
	}

	QTextStream out(&file);

	int count = 0;
	for (int n = 0; n < 1000; ++n) {
		// 枝が地面にぶつからないよう、ランダムに生成
		while (true) {
			renderManager.removeObjects();
			tree.generateRandom();
			if (!tree.generateGeometry(&renderManager)) break;
		}

		// render the tree
		render();

		QImage img = grabFrameBuffer();
		cv::Mat sourceImage(img.height(), img.width(), CV_8UC4, img.bits(), img.bytesPerLine());
		cv::Mat grayImage;
		cv::cvtColor(sourceImage, grayImage, CV_RGB2GRAY);

		// 白黒画像に変換
		cv::threshold(grayImage, grayImage, 100, 255, CV_THRESH_BINARY);

		// derivation木の各ノードについて、学習データを作成する
		std::vector<cv::Mat> localImages;
		std::vector<std::vector<float> > parameters;
		tree.generateTrainingData(grayImage, &camera, width(), height(), localImages, parameters);

		// 画像、パラメータをファイルに保存
		for (int i = 0; i < localImages.size(); ++i) {
			QString filename = resultDir + QString("image_%1.png").arg(count++, 6, 10, QChar('0'));
			cv::imwrite(filename.toUtf8().constData(), localImages[i]);

			for (int k = 0; k < parameters[i].size(); ++k) {
				if (k > 0) {
					out << ",";
				}
				out << parameters[i][k];
			}
			out << "\n";
		}

	}

	out.flush();
	file.close();
}

void GLWidget3D::generatePredictedData() {
	QString resultDir = "C:\\Anaconda\\caffe\\data\\pmtree2d\\pmtree2d_predicted\\";
	if (QDir(resultDir).exists()) {
		QDir(resultDir).removeRecursively();
	}
	QDir().mkpath(resultDir);

	QFile file("predicted_results.txt");
	if (file.open(QIODevice::ReadOnly)) {
		QTextStream in(&file);
		int n = 0;
		while (!in.atEnd()) {
			QStringList data = in.readLine().split(",");
			
			std::vector<std::vector<float> > params;
			for (int i = 0; i * 63 < data.size(); ++i) {
				std::vector<float> ps;
				for (int k = 0; k < 63; ++k) {
					ps.push_back(data[i * 63 + k].toFloat());
				}
				params.push_back(ps);
			}

			tree.recover(params);
			
			// 木を生成
			renderManager.removeObjects();
			tree.generateGeometry(&renderManager);

			// 画像を生成
			render();

			QImage img = grabFrameBuffer();
			cv::Mat sourceImage(img.height(), img.width(), CV_8UC4, img.bits(), img.bytesPerLine());
			cv::Mat grayImage;
			cv::cvtColor(sourceImage, grayImage, CV_RGB2GRAY);

			// 画像を縮小
			cv::resize(grayImage, grayImage, cv::Size(512, 512));
			cv::threshold(grayImage, grayImage, 200, 255, CV_THRESH_BINARY);
			cv::resize(grayImage, grayImage, cv::Size(256, 256));
			cv::threshold(grayImage, grayImage, 200, 255, CV_THRESH_BINARY);
			cv::resize(grayImage, grayImage, cv::Size(128, 128));
			cv::threshold(grayImage, grayImage, 200, 255, CV_THRESH_BINARY);

			// write the iamge to file
			QString filename = resultDir + QString("image_%1.png").arg(n, 6, 10, QChar('0'));

			cv::imwrite(filename.toUtf8().constData(), grayImage);

			n++;
		}
	}

}

void GLWidget3D::render() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// PASS 1: Render to texture
	glUseProgram(renderManager.programs["pass1"]);

	glBindFramebuffer(GL_FRAMEBUFFER, renderManager.fragDataFB);
	glClearColor(0.95, 0.95, 0.95, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderManager.fragDataTex[0], 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, renderManager.fragDataTex[1], 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, renderManager.fragDataTex[2], 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, renderManager.fragDataTex[3], 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, renderManager.fragDepthTex, 0);

	// Set the list of draw buffers.
	GLenum DrawBuffers[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
	glDrawBuffers(4, DrawBuffers); // "3" is the size of DrawBuffers
	// Always check that our framebuffer is ok
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("+ERROR: GL_FRAMEBUFFER_COMPLETE false\n");
		exit(0);
	}

	glUniformMatrix4fv(glGetUniformLocation(renderManager.programs["pass1"], "mvpMatrix"), 1, false, &camera.mvpMatrix[0][0]);
	glUniform3f(glGetUniformLocation(renderManager.programs["pass1"], "lightDir"), light_dir.x, light_dir.y, light_dir.z);
	glUniformMatrix4fv(glGetUniformLocation(renderManager.programs["pass1"], "light_mvpMatrix"), 1, false, &light_mvpMatrix[0][0]);

	glUniform1i(glGetUniformLocation(renderManager.programs["pass1"], "shadowMap"), 6);
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, renderManager.shadow.textureDepth);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	drawScene();

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Blur

	if (renderManager.renderingMode != RenderManager::RENDERING_MODE_LINE && renderManager.renderingMode != RenderManager::RENDERING_MODE_HATCHING) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		qglClearColor(QColor(0xFF, 0xFF, 0xFF));
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glDisable(GL_DEPTH_TEST);
		glDepthFunc(GL_ALWAYS);

		glUseProgram(renderManager.programs["blur"]);
		glUniform2f(glGetUniformLocation(renderManager.programs["blur"], "pixelSize"), 2.0f / this->width(), 2.0f / this->height());
		//printf("pixelSize loc %d\n", glGetUniformLocation(vboRenderManager.programs["blur"], "pixelSize"));

		glUniform1i(glGetUniformLocation(renderManager.programs["blur"], "tex0"), 1);//COLOR
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, renderManager.fragDataTex[0]);

		glUniform1i(glGetUniformLocation(renderManager.programs["blur"], "tex1"), 2);//NORMAL
		glActiveTexture(GL_TEXTURE2);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, renderManager.fragDataTex[1]);

		/*glUniform1i(glGetUniformLocation(renderManager.programs["blur"], "tex2"), 3);
		glActiveTexture(GL_TEXTURE3);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, renderManager.fragDataTex[2]);*/

		glUniform1i(glGetUniformLocation(renderManager.programs["blur"], "depthTex"), 8);
		glActiveTexture(GL_TEXTURE8);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, renderManager.fragDepthTex);

		glUniform1i(glGetUniformLocation(renderManager.programs["blur"], "tex3"), 4);//AO
		glActiveTexture(GL_TEXTURE4);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, renderManager.fragAOTex);

		if (renderManager.renderingMode == RenderManager::RENDERING_MODE_SSAO) {
			glUniform1i(glGetUniformLocation(renderManager.programs["blur"], "ssao_used"), 1); // ssao used
		}
		else {
			glUniform1i(glGetUniformLocation(renderManager.programs["blur"], "ssao_used"), 0); // no ssao
		}

		glBindVertexArray(renderManager.secondPassVAO);

		glDrawArrays(GL_QUADS, 0, 4);
		glBindVertexArray(0);
		glDepthFunc(GL_LEQUAL);

	}

	// REMOVE
	glActiveTexture(GL_TEXTURE0);
}

/**
* Draw the scene.
*/
void GLWidget3D::drawScene() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(true);

	renderManager.renderAll();
}

void GLWidget3D::keyPressEvent(QKeyEvent *e) {
	ctrlPressed = false;
	shiftPressed = false;
	altPressed = false;

	switch (e->key()) {
	case Qt::Key_Control:
		ctrlPressed = true;
		break;
	case Qt::Key_Shift:
		shiftPressed = true;
		break;
	case Qt::Key_Alt:
		altPressed = true;
		break;
	default:
		break;
	}
}

void GLWidget3D::keyReleaseEvent(QKeyEvent* e) {
	ctrlPressed = false;
	shiftPressed = false;
	altPressed = false;
}

/**
 * This event handler is called when the mouse press events occur.
 */
void GLWidget3D::mousePressEvent(QMouseEvent* e) {
	lastPos = e->pos();
	camera.mousePress(e->x(), e->y());
}

/**
 * This event handler is called when the mouse release events occur.
 */
void GLWidget3D::mouseReleaseEvent(QMouseEvent* e) {
	// do nothing
	updateGL();
}

/**
 * This event handler is called when the mouse move events occur.
 */
void GLWidget3D::mouseMoveEvent(QMouseEvent* e) {
	lastPos = e->pos();

	if (e->buttons() & Qt::RightButton) {
		if (shiftPressed) { // Move
			camera.move(e->x(), e->y());
		}
		else { // Rotate
			camera.rotate(e->x(), e->y());
		}
	}

	updateGL();
}

void GLWidget3D::wheelEvent(QWheelEvent* e) {
	// zoom
	camera.zoom(e->delta()); 
	updateGL();
}

/**
 * This function is called once before the first call to paintGL() or resizeGL().
 */
void GLWidget3D::initializeGL() {
	// init glew
	GLenum err = glewInit();
	if (err != GLEW_OK) {
		std::cout << "Error: " << glewGetErrorString(err) << std::endl;
	}

	if (glewIsSupported("GL_VERSION_4_2"))
		printf("Ready for OpenGL 4.2\n");
	else {
		printf("OpenGL 4.2 not supported\n");
		exit(1);
	}
	const GLubyte* text = glGetString(GL_VERSION);
	printf("VERSION: %s\n", text);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	glEnable(GL_TEXTURE_2D);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);

	glTexGenf(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
	glTexGenf(GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
	glDisable(GL_TEXTURE_2D);

	////////////////////////////////
	renderManager.init("", "", "", true, 8192);
	renderManager.resize(this->width(), this->height());

	camera.xrot = 0.0f;
	camera.yrot = 0.0f;
	camera.zrot = 0.0f;
	camera.pos = glm::vec3(0, 5, 15.0f);
}

/**
 * This function is called whenever the widget has been resized.
 */
void GLWidget3D::resizeGL(int width, int height) {
	height = height ? height : 1;

	glViewport(0, 0, (GLint)width, (GLint)height);
	camera.updatePMatrix(width, height);
	renderManager.resize(width, height);

	renderManager.updateShadowMap(this, light_dir, light_mvpMatrix);
}

/**
 * This function is called whenever the widget needs to be painted.
 */
void GLWidget3D::paintGL() {
	render();
}

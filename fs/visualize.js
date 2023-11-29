    // Based on https://www.reddit.com/r/javascript/comments/9nihtw/a_simple_3d_engine_in_47_lines_of_js/
    // Geometry of a Model
  const drawer = (canvasId) => {
    const emptyColor = '#bbb';
    const fillColor = '#00f';
    const canvasBgColor = '#f3f3f3';
    const percentageAttribute = 'tank_percentage';

    const cnv = document.getElementById(canvasId), ctx = cnv.getContext("2d");
    const canvasWidth = parseInt(cnv.getAttribute("width"), 10);
    const canvasHeight = parseInt(cnv.getAttribute("height"), 10);

    const mutationCb = (mutationList) => {
      for(const mutation of mutationList) {
        if(mutation.type !== "attributes" || mutation.attributeName != percentageAttribute) return;
        percent = parseInt(mutation.target.getAttribute(percentageAttribute), 10);
      }
    }
    const observer = new MutationObserver(mutationCb);
    observer.observe(cnv,{attributes:true});

    let percent = parseInt(cnv.getAttribute(percentageAttribute), 10);;
    const numPoints = 80;
    const cR = 0.7;
    const cH = cR*2;

    const getDimension = dim => vertex => vertex[dim];
    const getX = getDimension(0);
    const getY = getDimension(1);
    const getZ = getDimension(2);

    const decideColor = (pt, t) => {
      for(let i = 0; i<t.length; i++) {
        if(getY(pt[t[i]]) + cR <= (100-percent) * (2 * cR)/100) return emptyColor
      }
      return fillColor;
    }

    let calculatePointsOnCircle = (num, r) => {
      if (num == 0) return;
      let alpha = (2 * Math.PI) / num;
      let res = [];
      for (i = 0; i < num; i++) {
        let x = Math.round(r * Math.sin(alpha * i + Math.PI) * 1000) / 1000;
        let y = Math.round(r * Math.cos(alpha * i + Math.PI) * 1000) / 1000;
        res.push([x, y]);
      }
      return res;
    }

    const translate = (point, mat) => [getX(point) * getX(mat), getY(point) * getY(mat), getZ(point) * getZ(mat)];

    let circlePoints = calculatePointsOnCircle(numPoints, cR).map(([x, y]) => [x, y, cH]);
    let allPoints = [...circlePoints, ...circlePoints.map(point=>translate(point, [1,1,-1]))];

    let tesselateSpace = (pointsCloud, cylSegments) => {
      let poly = [];
      for (let i = 0; i < cylSegments; i++) {
        poly.push([i, i + cylSegments]);
      }
      return poly;
    }

    var polygons = tesselateSpace(allPoints, numPoints);

    update();

    function update() {
      var t = Date.now(), ang = t / 950, s = Math.sin(ang), c = Math.cos(ang);
      // var mat = [c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 10 + Math.sin(t / 320)];
      var mat = [c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 5];
      ctx.fillStyle = canvasBgColor;
      ctx.fillRect(0, 0, 400, 300);

      draw(allPoints, polygons, mat);
      window.requestAnimationFrame(update);
    }

    function draw(ps, ts, m) {
      let transVertices = ps.map(pt => vertexTransform(pt, m));
      ts.forEach(t=>fragmentShader2(transVertices, t, decideColor(ps,t)));
    }

    function vertexTransform(pt, m) {
      let x = getX(pt), y = getY(pt), z = getZ(pt);
      var x0 = m[0] * x + m[1] * y + m[2] * z + m[3];
      var y0 = m[4] * x + m[5] * y + m[6] * z + m[7];
      var z0 = m[8] * x + m[9] * y + m[10] * z + m[11];
      return [x0, y0, z0];
    }

    function fragmentShader2(vert, fig, col) {
      const halfWidth = canvasWidth / 2;
      const halfHeight = canvasHeight / 2;
      
      const project = v => [
        halfWidth + canvasWidth * getX(v) / getZ(v),
        halfHeight + canvasHeight * getY(v) / getZ(v)
      ];

      ctx.beginPath();
      let xyPoint = project(vert[fig[0]]);
      ctx.moveTo(xyPoint[0], xyPoint[1]);

      for(let i = 1; i <= fig.length; i++) {
        xyPoint = project(vert[fig[i%fig.length]]);
        ctx.lineTo(xyPoint[0], xyPoint[1]);
      }

      ctx.strokeStyle = col;
      ctx.stroke();
    }
  }

  drawer('tank-visualization');